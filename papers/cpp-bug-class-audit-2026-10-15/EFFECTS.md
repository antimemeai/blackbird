# Effect knowledge, domain refusal and capture failure

Staff-engineer class survey, 2026-10-15, baseline
`095d7d14fcbf68826febb87f52d910f49f080caf`. The previous Ultramarine repair is
background, not a candidate reopened for another assurance layer. No product,
test, configuration, issue database or dependency was changed by this lane.
Three small scratch engine scenarios ran against existing Debug libraries;
they used fresh private temporary journals and a fake participant transport.
They did not call a real provider or execute a child command. AFL, old sessions
and artwork were left alone. No broad test suite was run.

The findings concern two independent facts that an error code cannot express:
whether the requested effect happened, and whether its observations were retained.
Current code often knows these facts locally and discards the distinction while
crossing the generic operation boundary. All source line numbers below refer to
the baseline above.

## Prioritized findings

### E1 — ordinary participant pressure still stops the whole turn

**P1; high confidence; participant-start path directly reproduced.**

`src/participants.cpp:1008–1009` rejects `start` when resident runs reach32 or
live runs reach the configured concurrency, before preparing the new direction,
retaining its admission or constructing its worker. The default concurrency is2.
`src/participants.cpp:1215–1216` independently rejects `send` when the selected
run has16 queued directions or64 message receipts, before preparing/inserting
the new direction. Both throw plain `ErrorCode::capacity` without an owner-issued
refusal observation.

`src/coding.cpp:3459–3464` exempts process capacity and the explicit command-start
status, so neither participant guard qualifies. At3827–3840 the engine latches
`capacity_stopped_` and writes `capacity.stop` claiming unknown effect and rejected
bytes not retained. At3851–3862 it classifies the attempt unknown; at3987–3992 it
throws capacity instead of returning the ordinary tool result. The default
`programs/turn.lua` appends a function-call output only after the call returns.
This reproduces the original user-visible failure class in another owner.

The direct probe configures concurrency1, starts one fake transport that waits
for cancellation, then attempts a second start inside Lua `pcall`. It next calls
`context_stats` to distinguish an ordinary refusal from a poisoned turn:

```text
ACTIVITY participant_start completed · 47ms disposition=1
ACTIVITY participant_start stopped; outcome unknown · 31ms disposition=4
SECOND_PCALL=false
TURN=capacity:0
launched=1 unknown=1 capacity.stop=1
```

There is exactly one fake transport launch; the rejected second run never
dispatches. The follow-up is blocked. No provider conclusion is inferred from
the first participant's eventual cancellation. The send path is source-confirmed,
not separately executed by this scratch probe. Existing
`tests/participants_test.cpp:96–106,130–138` checks direct pool overflow but does
not attack the generic engine boundary that turns it into capture failure.

**Violated invariant:** a resource owner's proven refusal before publication is
an ordinary failed request if its result can be retained; it must not claim lost
capture or uncertain execution. Refusing the new direction says nothing about
prior accepted work and must not erase its obligations.

**Decisive permanent check:** a deterministic provider requests the second start
and receives `resource/count/limit/attempt/not_started` in its real function-call
output; it then successfully reads the first still-live run. A corresponding
full-inbox case preserves all accepted messages and rejects only the offered
message. Inject refusal-result retention failure separately and require the
existing blocked-writer fence. Do not exempt all participant capacity: worker
capture overflow, admission retention and raw credit use the same numeric error.

### E2 — configuration validation is mislabeled as uncertain execution

**P2; high confidence; directly reproduced.**

`src/coding.cpp:2200–2201` rejects a proposal with more than32 modules, or an
encoded proposal over65536 bytes, before constructing the candidate registry or
publishing the pending configuration at2248–2252. A proposal containing33 small,
distinct, syntactically valid `return {}` modules reaches this guard. The same
generic3827/3851/3987 capacity path marks it unknown and stops further work,
although the requested new configuration was never staged.

```text
ACTIVITY program_config stopped; outcome unknown · 55ms disposition=4
CONFIG_PCALL=false
TURN=capacity:0
launched=0 unknown=1 capacity.stop=1
```

The following `context_stats` call is blocked. This is an avoidable inability to
inspect/correct a bad proposal in the same workflow, with misleading capture-loss
language. It is not a claim that pending configuration would otherwise activate
midturn, and it does not authorize changing the32-module bound.

**Violated invariant:** input/domain validation limits do not imply failed audit
capture. Candidate rejection and failed preservation of the rejection need
distinct representations.

**Decisive permanent check:** propose33 valid modules; receive a retained ordinary
validation/refusal result and unchanged effective/pending revisions; submit a valid
proposal or perform a read in the same turn. Fail the retained result write and
require the existing capture fence instead. A local invalid-range classification
could address this particular input limit, but a blanket capacity rewrite would
repeat E1's mistake at another owner.

### E3 — task capacity has the same semantic collision

**P2; high confidence from source; no direct engine probe claimed.**

`src/tasks.cpp:233–235` rejects an add when its candidate task count reaches
`TaskState::max_items=2048` or the next ID cannot advance. At318–319, an existing
row whose version is `UINT64_MAX` also rejects in preparation. These are candidate
checks; `TaskStore::edit` calls `state_.prepare` at728, records the accepted packet
later at755, and commits state at769. The throwing candidates do not change the
task state. The engine nevertheless applies the same capture-stop/unknown path.

**Reachable trigger:** fill2048 current tasks using valid bounded batches, then
offer another add against the current base. Version exhaustion is the much less
practical second source token, not the primary trigger. Explicit task archival
and retry windows do not make2048 a lifetime limit; this finding does not demand
automatic eviction or removal of the bound.

**Decisive permanent check:** restore/build a valid full task state, request an
add through the engine, verify unchanged base/items and a normal retained domain
refusal; archive a selected eligible item and retry a distinct add in the same
turn. Use state restoration rather than thousands of unrelated engine operations
merely to reach the bound. Retention-failure injection must still block effects.

### E4 — allocation after a successful file replacement loses effect knowledge

**P2; high confidence; directly fault-reproduced.**

`src/tools.cpp:306` performs the actual file write/replacement; line307 constructs
the allocating `Value` result afterward. Both write_file and edit_file share this
path. The plain native writer's final successful operation is `rename` at68;
the traced writer separately marks successful replacement at
`src/raw_file.cpp:1029–1031`. A one-shot allocation failure in the result
construction occurs after the destination has changed.

`CodingEngine::Boundary::dispatch` catches `bad_alloc` as allocation at3579–3581.
The name-based classifier3851–3862 omits file writes/edits except for capacity,
so their terminal disposition remains failure, with no field distinguishing a
successful replacement from an untouched destination. The result is returned
normally and later operations can run.

The scratch executable interposes `rename` only for its fresh `/tmp/.../target`.
After the real rename succeeds, it arms exactly one global `operator new` failure;
the next allocating result construction throws. This does not simulate a failed
rename. The independent destination read occurs after the turn:

```text
ACTIVITY write_file failed · 31ms disposition=2
{"detail":0,"error":"allocation"}
ACTIVITY context_stats completed · 26ms disposition=1
FOLLOWUP_REACHED
TURN=success
INJECTED=1 ACTUAL_BYTES=actual effect
launched=0 unknown=0 capacity.stop=0
```

**Violated information invariant:** model/operator results must distinguish the
adapter's result-construction failure from the effect's success or uncertainty.
This is not a demonstrated automatic duplicate effect: foundation.hpp330–332
explicitly says adapter failure does not prove absence of an external effect,
and no automatic file replay was found. The defect is the loss of the more
precise fact before the presentation layer says only “failed.”

**Decisive permanent check:** one failure immediately after successful replacement;
assert exact new destination bytes, retained outcome that preserves applied or
unknown effect knowledge, and no automatic retry. Include edit_file on the same
shared path if its input preparation differs materially. Prebuild the success
result before replacement as the smallest repair for this exact allocation edge;
still preserve a post-effect state for later capture failures. Merely putting
file names into the generic unknown whitelist loses known-success information
and does not address future adapters.

## End-to-end representation map

| Boundary | What it currently owns | What survives or is lost |
| --- | --- | --- |
| Concrete guard in Participants/TaskState/configuration | Exact bound, counts, no candidate publication yet | Plain capacity loses resource and refusal provenance |
| File writer | Successful rename or failed native operation | Caller receives void; later result allocation cannot carry applied state |
| LocalTools/native body | Value result or thrown Error | Error contains only code/int64 detail; effect knowledge absent |
| RetainedState::dispatch2603–2635 | Open published before dispatch; adapter result and receipt-recording result separate | Correctly fences recording failure; adapter entry is deliberately not native effect start |
| CodingEngine::operation | Local Error, result original, terminal observation, selected special owner state | Operation-name whitelist reconstructs meaning that was known lower down |
| Lua default workflow | Call returns, then exact function_call_output is appended | Throw prevents ordinary result linkage; stop placeholder cannot restore missing semantics |
| TUI completion callback | String and AttemptDisposition | Can only render the already flattened disposition |

The expensive failure is not a missing catch. It is an owner losing knowledge and
another component guessing it from a code or operation name. More enum values in
the global ErrorCode alone would not preserve all relevant axes.

## Surveyed counterexamples and conservative behavior

- The repaired command-start status supplies a guard-local reason, counts and
  worker-start observation. Retention failure clears its exemption. This is a
  useful owned pattern; this survey does not reopen its settled implementation.
- `RetainedState::dispatch:2620–2635` preserves adapter and recording results
  separately and sets `recording_failed_` on receipt failure. Its dispatch flag
  intentionally means adapter entry, not “shell effect began.” Do not redefine it.
- `src/colleague.cpp:330–338` returns not_dispatched for preparation refusal and
  retains that wrapper; its final capture remains outside the transport catch.
  Post-transport unclassified errors at438–441 remain unknown. This is a better
  counterexample than assuming any thrown request was never sent.
- `src/beads.cpp:112–116` deliberately marks mutation possible before child start;
  unexpected failure therefore errs toward uncertainty. Its normal Error catch
  also distinguishes spawned writes from unavailable prelaunch work at164–173.
  Neither conservative false-positive uncertainty proves a duplicate mutation.
- `src/workflow_runtime.cpp:504–516` handles a pre-leaf stop as cancellation, but
  maps thrown LocalTools errors to unknown. Existing workflow_runtime_test
  explicitly expects unknown for a duplicate edit conflict. Without an owner
  supplied stage/result, do not “fix” arbitrary exceptions into safe refusals.
- `src/workflow_executor.cpp:157–164` can cancel a still-queued leaf without
  invoking it; that owner has the required stronger fact.
- `TaskStore::edit:796–804` catches presentation-callback failures after accepted
  task publication. It does not pretend committed work was undone.
- `WorkflowHost::request` can fail before send or after the full frame was sent
  (`src/workflow_host.cpp:583–619`). Its local error alone is not a safe-retry
  promise. No unsafe automatic host retry is claimed by this survey.
- Audit/trajectory/variables descriptions and actual `src/audit.cpp:248–383,
  398–407,418–423,558–581` agree on pinned end, ascending pages, scan limits and
  variable-query zero cursor. No additional pagination defect was found. Absence
  of a record in an uncompleted page still cannot establish absence of an effect.
- The provider retry loop records distinct attempt IDs and accepts only complete
  responses before the default program executes tools. Remote cost/requests may
  be repeated by explicit policy; this is not evidence of native command replay.

The requested Rhizome watch path `../rhizome/papers` is absent in this workspace;
no findings were invented from it.

## Literature and reference implementation implications

Exact acquired originals, revisions, authors, licenses, hashes and restoration
commands are in [EFFECT_SOURCES.json](EFFECT_SOURCES.json). They remain ignored
study-only reference files, not dependencies or imported source. No source archive
was extracted or modified; the acquired package consists of original individual
source/documents.

1. [gRPC's status contract](https://grpc.io/docs/guides/status-codes/) explicitly
   allows a deadline error after successful mutation and cautions against blindly
   retrying unavailable non-idempotent operations. Transfer the separation of
   response status and effect knowledge. It supplies no local command guarantee.
2. [FoundationDB's developer guide](https://apple.github.io/foundationdb/developer-guide.html#the-commit-unknown-result-error)
   distinguishes unknown commit from timeout/cancellation, including whether a
   transaction might still complete afterward. Transfer that distinction to
   local custody versus external effects. Child-group cleanup does not undo
   detached descendants, remote work or a successful file replacement.
3. [Asio's cancellation contract](https://www.boost.org/doc/libs/master/doc/html/boost_asio/overview/core/cancellation.html)
   distinguishes terminal, partial and total guarantees. The acquired1.85.0
   header and qbk make that separation explicit. It is a useful vocabulary for
   contracts, not a reason to install Asio or promise rollback on Ctrl-C. Native
   process cancellation cannot generally supply total/no-side-effect semantics.
4. [LLVM's error manual](https://llvm.org/docs/ProgrammersManual.html#error-handling)
   and [Lang Hames's2016 talk](https://llvm.org/devmtg/2016-11/Slides/Hames-Error.pdf)
   illustrate checked, typed errors. Important source-level qualification:
   acquired LLVM18.1.8 Error.h266–270 and715–719 gate unchecked-destruction checks
   on `LLVM_ENABLE_ABI_BREAKING_CHECKS`. The pattern catches discarded errors;
   it cannot tell whether a handled capacity was domain pressure or capture loss.
5. [Clang's typestate attributes](https://clang.llvm.org/docs/AttributeReference.html#consumed-annotation-checking)
   describe consumed/unconsumed/unknown transitions. They can flag misuse of
   annotated handles, but do not encode this multidimensional effect model or
   observe a syscall. A successful annotation is not a distributed-effect proof.
6. [clang-tidy's configurable return-value check](https://clang.llvm.org/extra/clang-tidy/checks/bugprone/unused-return-value.html)
   can cover selected native functions; its defaults do not provide a domain
   effect classifier. The owned Result is already `[[nodiscard]]` at
   foundation.hpp53/72. Recommending the same attribute again would miss these
   findings, all of which handle their local error.

These are engineering inferences from the cited mechanisms and actual source,
not assertions that the references solve Blackbird's specific classification.

## Prevention shortlist and costs

| Mechanism | Prevents | Cannot establish | Incremental fit/cost |
| --- | --- | --- | --- |
| Owner-only nonallocating domain-refusal type, returned separately from recording/transport failure | E1/E2/E3 lose their capacity-code ambiguity; only the actual prepublication guard constructs not_started | Successful retention of that refusal, absence of effects before an incorrectly placed guard | C++20 enum/struct/variant, no dependency; a few adapters first, encode to Value only at Lua/tool boundary |
| Separate native effect knowledge, local outcome and observation status | E4 and name-whitelist omissions; capture errors cannot masquerade as ordinary refusals | The actual syscall/remote outcome unless its owner observes it | Small owned typed outcome; avoid unbounded history or payload copies; current retained DispatchReport already separates two axes |
| Preallocate result and cleanup before irreversible effect; mark applied immediately after observed success | Exact E4 allocation window; makes later code distinguish successful rename from result-construction trouble | Remote rollback, host power-loss durability, all later recording faults | Build file success Value before rename; nonallocating progress scalar/token afterward; preserve raw failed-observation fence |
| Exhaustive visitor/switch over owned outcome variants; no catch-all success/refusal visitor | A new native outcome cannot silently inherit default failure/success | Correct construction/semantic truth of a chosen alternative | std::variant with explicit visitors, or enum plus -Wswitch-enum and Werror; fits both compilers; root owns actual diagnostic qualification |
| Typed operation category/descriptor instead of repeated string-name allowlists | Newly added effectful adapter omitted from one of several classification chains | Precise point at which each adapter starts an effect | Central owned registry entry with required effect contract; moderate migration, avoid giant whole-core rewrite |
| Targeted nodiscard/native-return checks | Dropped settlement/admission results and relevant native errors | A checked-but-misclassified result, assigned-then-unused Result, intentional void cast | Existing Result already nodiscard; qualify configured clang-tidy rules rather than enabling entire rule families; no suppressions adopted here |
| Narrow debug checked-consumption or typestate prototype for admission/settlement tokens | Locally forgotten settlement or repeated token use on supported paths | Semantic correctness across threads, callbacks, Lua, IPC, or remote effects | Candidate only: two compiler versions, release/debug behavior and move semantics need direct qualification; do not abort the live process merely to imitate LLVM |
| Boundary-specific semantic/fault fixture | Concrete refused-vs-started and applied-vs-result-failed errors despite static cleanliness | Every OS outcome or model interpretation | A few deterministic scenarios at the actual owner boundary; no mirrored tests for every if, no extra wrapper certification layer |

Adopt the first three concepts as design directions, not a dependency selection
or an unreviewed implementation. A suitable native model has an explicit refusal
observation, effect knowledge, and preservation status; a boolean `safe_to_retry`
would erase the distinctions again. A retained refusal is a fresh decision point,
not an automatic retry instruction. No compiler flag can manufacture information
already thrown away by an adapter.

## Probe reproduction and limits

Ignored files are under
`context/cpp-bug-class-audit-2026-10-15/effects/`: probe.cpp, build logs,
participant.log, config.log, file.log and probe-metadata.json. The metadata gives
the retained combined executable/source/library hashes and sysroot. The first
two runs preceded addition of the file fault hook; their initial scratch binary
hash was not captured. The retained hook is unarmed in those modes. Build command:

```sh
/opt/homebrew/Cellar/llvm/23.1.2/bin/clang++ -std=c++20 -g \
  -isysroot /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk \
  -Iinclude context/cpp-bug-class-audit-2026-10-15/effects/probe.cpp \
  build/debug/libblackbird_coding.a /opt/homebrew/lib/liblua5.4.dylib \
  build/debug/libblackbird_colleague.a build/debug/libblackbird_foundation.a \
  -o context/cpp-bug-class-audit-2026-10-15/effects/probe
```

Run that scratch executable with participant, config, or file. Each creates its
own0700 mkdtemp directory. The participant transport only increments an atomic
launch counter and waits for its supplied stop callback; shutdown joins it. The
file hook arms only after a successful rename whose destination ends `/target`;
one bad_alloc is consumed, then normal result retention proceeds. Logs state
actual observed effects and terminal dispositions. They are direct fault probes,
not production tests or Linux qualification. The initial manual compile omitted
the repository's explicit sysroot and failed in system-header discovery; setting
that same configured sysroot built successfully. No flags or product code were
changed. Timings in these logs are incidental and are not performance results.
