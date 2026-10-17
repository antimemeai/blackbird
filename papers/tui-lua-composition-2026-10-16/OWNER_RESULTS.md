# Generic Lua presentation owner results

2026-10-16. Implements the first conceptual unit of [PLAN](PLAN.md) and
[OWNER_PLAN](OWNER_PLAN.md). This component provides asynchronous typed scene
composition and native accepted action envelopes. It does not dispatch domain
operations, read source files, integrate the terminal instrument, capture raw UI
publication or complete all Unit2. Those owners remain separate.

The public API is include/blackbird/tui_lua.hpp in namespace blackbird::tui::lua.
It includes only reusable native scene/foundation/raw-reference machinery, no
command/task/participant/engine API. Source reuses the existing terminal_lines
sanitizer through display_text.hpp; root moved that existing declaration without
changing its definition. No new dependency or sanitizer implementation is added.

## Execution, construction and failure

One worker exclusively creates/runs/closes a fresh Lua5.4.8 VM for each invocation.
Immutable Script retains exact original source, bounded65536 bytes. Presentation
input copies at most8 actual rows/targets and two4096-byte literal windows with
separate512-byte titles. Dependencies carry actual environment/conversation/view,
source generation, request sequence, base scene revision, local-input revision
and query revision. Callback data never points into a live domain owner.

The native builder uses positional exact types and finite limits, not arbitrary
Lua table traversal or source stringify/reparse. Collections/items/inspectors
copy typed properties; keys/indices/targets are checked before append. Aggregate
inspector source text allows16384 bytes so the8192 actual original bytes plus
Lua-authored separators/headings fit; emitted safe text/row/span counts still obey
adopted SceneHost Limits{4,32768,196608}. At most8 items and4 nodes are constructed.
Default heap16MiB/instruction100000 limits are documented workload bounds, not
latency or whole-process memory guarantees. Lower configured limits exercise
actual allocator admission; settings cannot exceed the adopted maxima.

The module is a plain raw-validated table containing compose plus at most8 named
functions. Outcome carries its sorted finite handler catalog, which the later
native adapter can adopt together with candidate/source. Preview does not imply
handler activation. Input accessors return exact finite literal bytes/titles;
rendering uses the shared safe text service. Composer/draft writes are absent.
The pure checked publish function rejects any dependency mismatch before actual
SceneHost publication; it has no application raw-capture authority.

Lua setup, source loading and calls run behind protected Lua boundaries. Native
work catches exceptions only while using nonthrowing Lua inspection APIs; owned
native temporaries end before Lua error/result pushes. No C++ exception escapes a
lua_CFunction and C-built Lua longjmp does not cross those native owner scopes.
The allocator uses actual osize only for nonnull pointers, checks live-byte budget
arithmetic, preserves old allocation on realloc failure, and never calls raw or
domain services. Allocation failures remain sticky even if Lua pcall catches one.
The VM has no user userdata or __gc/__close registration path at all.

A rooted coroutine/count-hook zero-result yield stops pure Lua, including nested
pcall loops. Budget, stop and supersession reasons remain native. Native callbacks
check stop/supersession before constructor/admission work too. A native callback is
still not instruction-preemptible; its bytes/fields/algorithm are bounded and it
never invokes arbitrary Lua, domain work, filesystem I/O or capture waits.

The initial profile exposes primitive base operations and pcall; no xpcall,
metatable/debug/package/loaders/coroutine/print/warn/collectgarbage, raw metatable
access or tostring. String len/sub/byte/char/lower/upper/reverse remain. Pattern,
gsub, format, dump/packing/repetition functions are stripped from the ORIGINAL
string library table in place, including the table reachable through string
metatable.__index. ui.find is literal-only, with finite16384/4096-byte arguments.
Table convenience and math libraries are absent; authored pure-Lua algorithms
remain possible under the instruction budget.

## Queues, lifetime and accepted envelope handoff

One active invocation, one latest pending composition, two pending discrete
handlers and one ready outcome bound native scheduling. Mailbox methods try-lock;
the worker releases the mailbox mutex before all Lua/allocation work. It does not
start another invocation until its previous outcome is consumed. Handler outcomes
are never coalesced. Stop is cooperative; actual VM close precedes the stopped
acknowledgment, and destruction joins the actual worker. Pending submissions stay
available as explicit stopped outcomes while the owner remains alive. No detach,
foreign live-VM access or assumed thread termination is used.

Only native handler phase may call ui.request(verb), using that handler's frozen
target, current native input origin and opaque Trigger. Lua cannot provide an
alternate target, manufacture a token or change phase. All callback failures stay
sticky even if caught by Lua; failed composition cannot publish a partial scene.
An already accepted envelope remains accepted when later Lua code errors or runs
out of memory; Outcome preserves the accepted-request count beside the error.

ActionInbox has8 finite pending slots and returns copies in monotonic acceptance
order. Native acknowledge releases a pending slot only after the consumer owns
the envelope and real domain reference/unknown disposition. It is not domain
success. Each opaque Trigger retains at most8 canonical envelopes under the same
inbox mutex. Identical delivery returns its original acceptance without requeue,
even after acknowledgment; changed target/verb/handler/dependencies conflict.
After all native token copies disappear no legal handler redelivery can recreate
that token. Pending accepted envelopes remain in the independent inbox. History
pins no VM, worker, domain Job or view. Native owner identity is independently
checked using a live identity object and a checked process-local serial, rather
than trusting caller-reused identity bytes. ActionKey carries that owner serial,
trigger serial and per-handler ordinal; counters saturate rather than wrap.

The inbox incarnation bytes are supplied by its native application owner and must
identify the intended runtime scope. This component provides no cross-process
persistence/reconstruction of Trigger. Domain broker retention/reconciliation and
actual operation references remain the next owning unit, not an exactly-once
external-effect claim.

## Runtime RED and targeted results

The permanent semantic fixture compiled and linked successfully against an
explicitly refusing API scaffold, then exited1:
`new-contract RED: valid script must produce actual native candidate`.
This is labeled a new-contract runtime RED, not old-source behavior. Root's
separate actual-old-main route RED covers missing product behavior. Exact
scaffold/header/test copies, qualified-library/source hashes and logs are retained
in context/tui-lua-composition-2026-10-16/owner/red-* and new-contract-red.log.
An initial missing test include failed compilation and was corrected before this
valid runtime RED; it is not counted as behavioral evidence.

The real implementation then passed all8 private native modes in green-01.json:

* semantic:8 actual typed targets, separate detail titles/exact bytes, actual safe
  scene publication, source/local-input stale rejection and exact named catalog.
* phases: caught module/compose/preview request attempts and invalid constructor
  stay native failures and produce zero accepted envelopes.
* actions: actual frozen target/origin, same-token duplicate and changed-target
  conflict, acceptance survives later Lua error, saturation without eviction,
  acknowledgment, duplicate/conflict after handoff,12 sequential new handoffs,
  slot-reuse acceptance order and cross-inbox token refusal/owner retirement.
* budgets: actual ordinary/nested-pcall loops return Budget, then fresh invocation
  globals do not leak across calls.
* profile: forbidden finalizer/loader/coroutine/callback helpers absent, including
  string method-index bypass; finite primitive string functions and pcall work.
* capacity: caught fifth-node refusal is sticky, full two-window newline/control
  expansion fits actual candidate limits, overlong source refuses before compile.
* allocations: real Lua setup cap, real heap-growth exhaustion and native builder
  allocation denial return explicit Allocation without a partial candidate.
* lifecycle: real builder allocation pause leaves mailbox serviceable; only latest
  unstarted composition survives, discrete handler outcomes both persist, actual
  stop/quiescence/join leave independent accepted actions retained.

Private build uses LLVM23.1.2/C++20, arm64/Xcode SDK, production warning flags,
Lua5.4.8 static library and root's APFS-cloned qualified native archives. The Lua
include directory is classified as system exactly as the configured project
profile; an initial manual -I failed the external C header macro diagnostics and
was corrected to -isystem without suppressions. Exact command is owner/build.sh;
no shared Ninja/CMake or archive mutation occurred. Root configured the actual
owning translation units for clang-tidy. Final cleanup rerun/tidy/review results
are appended when complete; no broader qualification is claimed by initial GREEN.


## Final owner qualification and review disposition

The final implementation/header/permanent-oracle copies and SHA-256 identities
are retained in owner/final-{tui_lua.hpp,tui_lua.cpp,tui_lua_test.cpp} and
owner/final-source-identities.json. Final private Debug execution passed all8
modes (green-final.json); each had the existing20-second per-mode watchdog.
The actual configured source and oracle translation units both passed
clang-tidy with no diagnostics, using `clang-tidy -p build/debug` and the project's
owning compile commands (source-tidy-final.log and test-tidy-final.log).

The sole independent source/oracle review found no production defect and two
permanent-oracle gaps. Both were corrected and dispositioned by that same
reviewer: the first detail window now uses an explicit extent containing embedded
NUL plus END and independently asserts the complete escaped suffix; lifecycle
now proves a genuinely completed unread outcome survives later submissions,
checks distinct sequence10/11/12/13 identities and handler FIFO, then establishes
an active allocation-paused handler21 plus pending22/23 and drains all three
exact stopped outcomes after native stop. The accepted inbox remains independent.
This is one review layer, with no additional review panel.

Before qualification, a concrete stop-parking race was corrected within this
owner: notifying a condition variable without taking its predicate mutex could
lose an idle stop between predicate check and wait. The worker now clears an
atomic_flag before checking persistent mailbox/stop state and uses C++20 atomic
wait; native notifiers set the flag and notify. Only the worker waits. An idle
stop and the active-plus-pending stop are both exercised by lifecycle. No timer,
detached worker or blocking producer operation was added.

All8 final owner modes also passed private Mac ASan/UBSan and TSan runs. The
actual new source and oracle were compiled with the respective sanitizer flags
and linked against APFS-cloned already-qualified native archives, whose exact
identities are saved in owner/sanitizer-archive-identities.json. Each profile's
command.txt, build.log, per-mode logs, results.json and source-identities.json are
under owner/asan or owner/tsan. ASan/UBSan uses halt_on_error and nonrecovering
undefined behavior checks; TSan uses halt_on_error and exitcode66. Both report
all8 exits0 with no diagnostics. The installed C-built Lua5.4.8 static archive is
the same existing dependency; these runs instrument the new C++ owner/oracle and
qualified native dependencies, not a rebuilt Lua runtime. No shared sanitizer
build directory, archive, timeout or source assertion was changed.

Linux owning-profile qualification is pending root's stable integrated-source
snapshot; no in-progress adapter closure has been copied or edited to obtain it.


## Final integrated Linux owning-profile qualification

The stable integrated source was snapshotted once by scripts/check-linux and
built in its independent owned Linux Release directory with Clang18.1.3,
libstdc++13 and checksum-verified Lua5.4.8. All25 owning cases passed in0.89s:
VM8, native instrument6, action bridge5, command delivery5 and the affected
reusable inspector1. This is one integrated profile lane, not25 repeated runs.
The exact command is owner/linux-25/command.txt; the source SHA-256 manifest,
driver output and exit0/timing are alongside it. Full compiler/CTest evidence is
context/linux/run-pz7tdbar/checks.log. Remote temporary source/build cleanup
completed normally. The VM header/source/oracle hashes match the private Debug,
ASan/UBSan and TSan qualified unit; no VM change was made for this profile.

The instrument's separate single review closed six concrete implementation
findings after fixes: owned metadata before worker enqueue, actual effective
source references after rejected/preview proposals, joint DATA/CONTROL admission,
typed selected-target resolution, matching-page pinning and missing-default
source recovery. It also dispositioned explicit unknown-on-runtime-exit4098
retention and the independent two-pin detail binding. These are adapter review
results; the generic VM's own independent review remains the sole review already
recorded above. Source gates, whole-application performance and final publication
remain root-owned integration checks.
