# Registry capacity, custody and reclamation survey

Audited source: `095d7d14fcbf68826febb87f52d910f49f080caf`.
This is a new search for analogous defects across owners, not another review of
the completed Ultramarine repair. Product source, tests and configuration stayed
unchanged. Two direct scratch episodes confirmed two new findings. No real
provider, operator session, fuzz output or artwork was accessed.

## Findings

### R1 — P1: participant archive can discard an emergency original before retention

**High confidence; deterministic public-API reproduction.**
`src/participants.cpp:1391–1415` calls `drain()` before selecting/joining the
worker, checks only `run.finished`, joins and cleans up, then erases the `Run`.
The worker can finish after that first drain. Its overflowing source is owned
by `run.emergency` (`497`); normal captures instead have independent ownership
in the pool's linked list (`500–507`). The emergency is therefore destroyed by
`runs.erase` before `drain()` can retain it (`905–947`).

Reachable production sequence:

1. A context-only participant finishes one response and has an addressed
   follow-up queued. The first response publishes waiting state and makes the
   snapshot dirty (`657–679`). The follow-up can then run without another owner
   action (`681–705`).
2. The owner calls `participant_archive`. Its first drain finishes the current
   capture queue and starts saving the dirty snapshot (`956–958`, `783–807`).
3. During that save, the worker acquires an extent crossing the 8 MiB pending
   capture budget. The already-acquired source moves into its emergency slot;
   overflow stops the participant and it publishes `finished` (`484–526`,
   `709–742`). A single 9 MiB HTTP response is an existing native fixture case
   in `tests/participants_raw_test.cpp:405–505`.
4. Archive sees finished, joins the actual worker return and destroys the run.
   There is no second drain or emergency check. Its final snapshot succeeds.
   The emergency's semantic retention callback was never called and `debt()`
   reports false (`213–220`).

The required invariant is **native worker return does not discharge retained
original debt**. An emergency owner must survive until its exact-used original
is retained, or an unknown-used original remains explicitly unresolved.
`finished`, `cleanup_closed`, and `joined` answer different questions.

The scratch probe uses the real `ProviderOriginalCapture::prepare/complete`
interface with a fake transport and a 9 MiB owned exact-used extent. Public
Publish/Save callbacks establish the schedule: first waiting publication pauses;
the archive drain's Save callback releases the follow-up and waits for its
terminal publication. The Save callback performs no reentrant participant
control. The API prohibits such reentry; it does not prohibit yielding. This
only widens a naturally reachable scheduling window: even an ordinary Save can
perform retained I/O while the worker runs, and the mutex is released around
that callback (`793–800`). The same ordering can happen when the owner is
preempted between saving the snapshot and acquiring archive's next lock.

Observed, exit 0:

```text
calls=2 large_retained=0 debt=0 rows=0
```

This confirms lost **semantic original ownership and a false clean-debt result**.
The probe leaves raw capture inactive. In production with raw capture active,
the physical acquired bytes can still be present in raw records; this is not a
claim that all disk evidence disappears. No ordinary semantic drain can recover
the erased emergency, and its run no longer advertises the debt.

Decisive regression: retain the same two-direction schedule, assert the exact
9 MiB payload reaches Retain once before successful archive, or that archive
refuses and keeps the emergency readable as debt. Include unknown-used and a
throwing Retain outcome: neither may erase the owner or report clean. Do not
"fix" this by retrying the remote request. A small fake transport is sufficient;
real network/provider access buys no relevant evidence.

Repair direction for the owning plan: close the worker's publication opportunity
by joining before final debt inspection; drain or refuse while preserving the
run if an emergency remains. Keep the existing shutdown/semantic-retention
separation and original identity. Do not change the 8 MiB bound to hide the race.

### R2 — P2: permanent delivery exhaustion becomes indefinite UI mailbox deferral

**High confidence; direct native reproduction.**
`src/command_jobs.cpp:1769–1774` puts three reasons into one skip-and-continue
branch: 64 KiB pending input, 4096 retained delivery identities, and 16 MiB
cumulative retained input identity payload. Only pending input normally shrinks.
The other two persist for the entire resident job (`1632–1678`;
`docs/COMMAND_JOBS.md:80`, `122–123`). A live job cannot be archived to release
that history. New mailbox input/signal/resize entries can consequently remain
accepted but unselected until the process terminates.

`request()` checks mailbox slots and a single input's size, then reports success
(`1702–1739`), without identifying permanent budget exhaustion. The terminal
clears `child_input` on that success (`src/terminal.cpp:1665–1678`) and records
new child dimensions on resize enqueue (`2246–2253`). Those bytes are now only
queued in the mailbox, with no present path to delivery. Repeated requests fill
the ordinary 60-slot mailbox. The owner only executes entries returned by
`requests()` (`src/coding.cpp:2623–2626`), so it cannot report a capacity refusal
for an entry that remains skipped forever.

Direct episode: start one live sleep PTY, deliver 4096 unique zero-byte inputs
(each completes immediately), then compare a direct control against the mailbox.
Zero bytes make the retained-identity boundary cheap and deterministic; a real
interactive child reaches the same boundary through ordinary input flushes,
resize operations and signals. Observe two mailbox selections with unchanged
state, then enqueue an urgent stop:

```text
direct capacity=1 active=1
first mailbox batch=0 second=0
after urgent stop batch=1 op=stop
```

This is **not** a claim that the intentional dedup bounds must be removed or
that the entire application cannot be recovered. Urgent stop/background have
reserved mailbox capacity and bypass this delivery gate. It is a lost-feedback
and progress defect for accepted input/resize/signal; the terminal can look
unresponsive while the child remains alive.

Decisive regression: saturate each permanent budget independently; an input or
signal must receive an explicit refusal or become deliverable through a stated
reclamation mechanism. Preserve duplicate exact-request replay at saturation,
conflicting identity rejection, transient-full input retry, unrelated-job
progress, and urgent stop/background. A no-byte identity case needs one child,
not 4096 subprocess launches. The 16 MiB case needs actual accepted bytes and a
reading PTY fixture so it does not confuse lifetime exhaustion with OS pressure.

Repair direction: make the owner distinguish `admit`, `wait_for_pending_input`,
and `refuse_permanent_budget`; do not put a nondecreasing budget into an
unqualified wait branch. Admission must account for queued reservations, not
only already-delivered controls, and UI success must state what was accepted.
Dropping receipt identities would allow duplicate writes/signals and is unsafe.

## Owners searched and counterexamples

| Owner inspected | Actual bound/lifetime | Outcome of this survey |
| --- | --- | --- |
| `src/participants.cpp`, `include/blackbird/participants.hpp`, native/raw participant tests | 32 resident runs; explicit completed-run archive; 16 queued and 64 retained direction identities; 1024/8 MiB capture queue plus per-run emergency | R1. The 32-run limit alone is not a new bug: archive is an explicit reclamation API. Message receipts deliberately preserve idempotency. Normal linked-list captures survive run erase; `Capture::run` is not dereferenced by drain, so no invented dangling-pointer finding. |
| `src/command_jobs.cpp`, header, native/engine tests; terminal and engine mailbox callers | 8 live/32 recent rows; 4096 delivery identities; 64 KiB pending versus 16 MiB cumulative input | R2. Settled recent-row reclamation was already repaired and is not reopened. Explicit joins in wait/settlement/archive/shutdown occur after lock scope ends. |
| `src/workflow_executor.cpp`, header, executor tests | Fixed slots include queued/running/completed-unpublished; successful publication frees slot and increments generation | Useful owned counterexample: drain preserves completion on throw/refusal; local `shared_ptr<Task>` keeps payload/callback destruction outside locked slot release. Stop callbacks and joins are outside mutex. Generation exhaustion intentionally retires a slot instead of wrapping identity. No new defect established. |
| `src/workflow_runtime.cpp` | Stable map nodes own tasks/joins; task waiters borrow join addresses; no erase path | No reclamation/dangling-waiter defect established. Future erasure must explicitly detach `Task::waiters` and `dirty_joins`; adding erase alone would invalidate this present invariant. |
| `src/workflow_store.cpp`, header, store tests | 65,536 cells and distinct submissions; durable receipts persist/replay | A real cumulative semantic capacity, unlike executor residency. Duplicate submissions read original request/events before accepting equality (`441–480`). Evicting them would change transaction dedup guarantees. Treat scalability/segmentation as a design constraint, not a confirmed accidental cache quota. |
| `src/workflow_join.cpp` | Descriptor max-members and max-observations; committed selection stabilizes reducer | Explicit bounded retained reducer history, not a recycled execution queue. No new defect established. |
| `src/tasks.cpp`, header, task tests | Current task projection plus a 16-receipt retry window; explicit archive | Receipt deque pops its oldest member; stale-retry behavior is explicitly checked. Current items and archived originals are distinct owners. No new lifetime quota established. |
| `src/station.cpp`, `docs/STATION.md:33–36`, `68` | 4096 event IDs and 4096 command IDs persist across reopen | Explicit session-wide dedup limits. Completed event outcomes stay in the table by design; unknown IDs must not be evicted. No accidental resident-capacity finding. Operational extension requires durable receipt indexing, not merely `erase`. |
| `src/workflow_host.cpp:280–299`, `339–414` | Pending request/reply bytes and live client slots | Input release and disconnected-client reap subtract pending bytes; clients erase after closure. No lifetime quota established. |
| `src/raw_runtime.cpp:890–949`, `1330–1346`, `1776–1808`; `src/raw_audit.cpp` | Active lane bound; acknowledged close retires lane; raw-prefix history per segment | Retirement checks published=acknowledged=preserved, rotates to a successor carrying active prefixes, then erases/unlinks. This prevents prefix history from becoming a lane lifetime quota. No new defect established. |
| `src/chat_view.cpp:232–255`, terminal history trimming | Derived display rows/bytes evict old presentation; originals live elsewhere | A projection with explicit trimming, not effect ownership. Text semantics assigned to another audit lane. |
| `src/request_cache.cpp` | Request assembly cache policy metadata | Despite its filename, no resident cache registry or lifetime cap exists here. |
| `src/workflow_main.cpp:83–108` | Signal waiter destructor wakes and joins one thread | No product mutex held at join; subprocess/signal detail sent to root's independent owner. |

Searches covered `include/`, `src/`, relevant `programs/`, and direct owner tests
using capacity guards, container removal, threads, condition variables, waiters,
receipt/history counts, and all explicit native `.join()` occurrences. This is a
bounded class survey, not a claim of exhaustive correctness. The requested
Rhizome rigor-watch paths `../rhizome/papers` and
`/Users/patrickbeam/projects_old/rhizome/papers` were unavailable in this checkout;
no findings are attributed to an unread corpus.

## Prevention mechanisms grounded in source

The useful narrowing is to encode **what a bound limits, who can release it,
and whether native completion actually ends custody**. Increasing a number or
adding another generic sanitizer run does not encode those facts.

| Mechanism | Concrete coverage and fit | Limitation/cost |
| --- | --- | --- |
| Separate pending reservation from durable receipt budget in owned types; return a closed admission disposition | R2: a function returning `admit / defer_pending / refuse_history` cannot silently use the same boolean for temporary and permanent exhaustion. A move-only reservation accounts for queued controls until owner consumption/cancellation. C++20 on both compilers; translate the closed result to native Lua values only at its boundary. | Types do not invent correct release rules. Keep actual receipt history intact; queued duplicates need equality handling. A small owner-specific type is preferable to a generic framework. |
| Extract retired owner under mutex; join/drain/final destruction outside; require a settled-custody predicate | R1 and original job-retirement class. Require terminal worker return **and** no emergency/unretained extent. Existing `WorkflowExecutor` is an owned example of release with surviving local ownership. | Destruction can invoke callbacks, release raw handles or block. A `shared_ptr` proves object survival, not which thread performs its last destruction. Final semantic drain can fail; failure must leave an owner. |
| Assert accounting identities at existing transitions | Check occupied slots = queued+running+completed, retained-byte charge equals surviving owners, and successful archive leaves neither an emergency nor unreported debt. Require any deferral to name an outstanding action that can release its needed budget. | Counters must describe actual owners; duplicating another shadow registry adds failure modes. Assertions should read current owner state at publication/reclamation transitions, not scan history on every keystroke. |
| Direct transition oracle with small capacities and schedule barriers | R1/R2: release worker after initial owner drain, fail retention, saturate history while pending=0, duplicate at full, and reuse a released slot with a stale handle. Reuse existing C++ fixtures/TSan profiles. | TSan sees data races, not this correctly locked lost-debt schedule or permanent-but-polite deferral. No extra fuzz campaign needed to establish these claims. |
| Clang capability annotations around an owned mutex wrapper and explicit blocking methods | `GUARDED_BY`, `REQUIRES`, `EXCLUDES` and scoped capabilities can check lock discipline at extraction/cleanup APIs. Both Clang18 and23 support the documented capability system. Root owns installed diagnostic probes. | Existing `std::mutex` use is not magically analyzed. `EXCLUDES` is opt-in and can miss transitive calls; implicit last-reference destructors and semantic debt are not proved. Header/wrapper retrofit and analysis of selected owners must earn their maintenance cost. [Clang18 manual](https://releases.llvm.org/18.1.8/tools/clang/docs/ThreadSafetyAnalysis.html). |
| Narrow custom AST rule if annotation retrofit is chosen | Flag direct `join`, `request_stop`, or annotated blocking cleanup from a held product capability; flag erasing owning `Run`/`Job` containers unless ownership is extracted first. Both build compilers expose Clang AST tooling; keep this development-only. | This rule does not exist in the repository and is a proposal, not installed protection. Ordinary AST matching cannot establish which `shared_ptr` is last or prove queue progress. A lexical blacklist alone would flag safe erasures. Prefer annotated explicit APIs before custom dataflow machinery. |
| Existing clang-tidy use-after-move and analyzer checks | Existing `.clang-tidy` already enables `bugprone-use-after-move` and `clang-analyzer-*`; useful around extracted owner/node handles. [Pinned LLVM18 check documentation](https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-18.1.8/clang-tools-extra/docs/clang-tidy/checks/bugprone/use-after-move.rst). | Flow sensitivity does not prove semantic retention or asynchronous ownership. These confirmed defects are not use-after-move. No new blanket rule is claimed to catch them. |
| Do not sell `bugprone-infinite-loop` as a mailbox liveness check | Its actual target is obvious loops with unchanged local nonaliased conditions. [Pinned LLVM18 documentation](https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-18.1.8/clang-tools-extra/docs/clang-tidy/checks/bugprone/infinite-loop.rst). | `requests()` terminates normally on every call. Its stuck work lives across calls in class members and an external owner loop; that is outside this diagnostic. Enabling it would not establish an R2 guard. |

Reference implementation lessons:

- **RocksDB v9.10.0** explicitly separates cache membership, external references,
  and deletion. `EraseUnRefEntries`, `SetCapacity`, `InsertItem`, `Release`, and
  `Erase` detach/account under mutex, then free or notify outside it. The LRU
  contains only unreferenced candidates, and strict-capacity insertion returns
  `MemoryLimit` when eligible eviction cannot satisfy the charge. This supplies
  an exact model for narrow owned reclamation and explicit refusal; it does not
  justify adopting a database/cache dependency. [Pinned source](https://raw.githubusercontent.com/facebook/rocksdb/v9.10.0/cache/lru_cache.cc).
- **LevelDB1.23 is a useful counterexample to copying patterns blindly.** Its
  in-use/LRU split is valuable, but `Release` calls `Unref` while holding its
  mutex, and `Unref` can execute the deleter. Its capacity can also be exceeded
  by pinned entries. Those are different callback/bound contracts from
  Blackbird's blocking worker/raw cleanup and strict resource policy.
  [Pinned source](https://raw.githubusercontent.com/google/leveldb/1.23/util/cache.cc).
- **Linux6.12 kref documentation** ties references to every real borrowed
  lifetime, including lookup+reference acquisition ordering, and shows separating
  list removal from final release. The transferable rule is to preserve an owner
  while asynchronous use or unresolved retention remains. Kernel `kref` itself
  is not a userspace dependency. [Primary documentation](https://www.kernel.org/doc/html/v6.12/core-api/kref.html).
- **Linux6.12 workqueue documentation** treats forward progress separately from
  queue capacity: scarce execution contexts must still permit the work that frees
  resources; destruction must account for work not currently pending on its
  queue. The corresponding Blackbird question is whether a deferred control has
  an independent release path, and whether joined work can still leave semantic
  debt. This does not propose kernel workers, rescue threads, or pool expansion
  for the present defects. [Primary documentation](https://www.kernel.org/doc/html/v6.12/core-api/workqueue.html).

Prioritize a truthful owner-specific admission result and post-join custody
predicate before generic tooling. They directly encode the missing distinctions;
capability diagnostics then reduce the chance of violating their lock contracts.

## Artifacts and exact scratch commands

Ignored scratch directory:
`context/cpp-bug-class-audit-2026-10-15/registries/`.
Both probes linked the already built Debug libraries; no production rebuild or
broad test run occurred. The first mailbox compile omitted the active Xcode SDK
and failed on the default missing SDK; a second compile corrected a scratch-only
`Value::field` typo. Neither executed a probe. The exact successful command shape
was:

```sh
/opt/homebrew/Cellar/llvm/23.1.2/bin/clang++ -std=c++20 -g \
  -isysroot /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk \
  -I include context/cpp-bug-class-audit-2026-10-15/registries/mailbox-lifetime.cpp \
  build/debug/libblackbird_foundation.a \
  -o context/cpp-bug-class-audit-2026-10-15/registries/mailbox-lifetime
context/cpp-bug-class-audit-2026-10-15/registries/mailbox-lifetime \
  > context/cpp-bug-class-audit-2026-10-15/registries/mailbox-lifetime.log 2>&1
/opt/homebrew/Cellar/llvm/23.1.2/bin/clang++ -std=c++20 -g \
  -isysroot /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk \
  -I include context/cpp-bug-class-audit-2026-10-15/registries/participant-retirement.cpp \
  build/debug/libblackbird_colleague.a build/debug/libblackbird_foundation.a \
  -o context/cpp-bug-class-audit-2026-10-15/registries/participant-retirement
context/cpp-bug-class-audit-2026-10-15/registries/participant-retirement \
  > context/cpp-bug-class-audit-2026-10-15/registries/participant-retirement.log 2>&1
```

SHA256:

| Input | Hash |
| --- | --- |
| `mailbox-lifetime.cpp` | `6c4a470b3845eef6d18a664a3e20968ad44283112403341d825b2913cd28ba73` |
| `participant-retirement.cpp` | `53506d636b0f9ab30720c0ce7352e6da2552f5c13261fc9e38d4ad60bb970268` |
| `build/debug/libblackbird_foundation.a` | `a013b56e35fa4061b6bceaad9216a4b565f137b3c4aabcb9bbd4574bc2c3a5a9` |
| `build/debug/libblackbird_colleague.a` | `90dbdb08ba486b05f06201e1468f0cd15e31c6b09d033ca345e622d612a4fa49` |

Acquired intact primary sources and license texts are inventoried in
[registry-sources/MANIFEST.json](registry-sources/MANIFEST.json), with acquisition
UTC, exact source URL/tag, bytes, SHA256 and restoration command. No extracted
Git metadata exists because these are direct source-file acquisitions, not
clones. The [source notes](registry-sources/README.md) distinguish studied code
from any proposed adoption.
