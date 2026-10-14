# Workflow design review resolutions

2026-10-14. Three independent layer-one attacks addressed native custody,
durability and expressiveness. Root integrated their concrete repairs into
[the design](../../docs/CONCURRENT_DURABLE_WORKFLOWS.md) and
[delivery sequence](../../docs/WORKFLOW_DELIVERY.md). Each owner performed one
finding-focused layer-two recheck. This is behavioral design review, not a product
qualification or a reason to rerun historical source/test campaigns.

## Native ownership

Read [attack](review-native.md) and [focused recheck](recheck-native.md).

| Finding | Final design behavior |
| --- | --- |
| N1 Recovered dispatch authority | Existing recovered-attempt fence stays intact. A trustworthy proven-unreleased frontier permits a recorded successor attempt under the original logical invocation, with fresh current release guards. Uncertain history remains unresolved. |
| N2 Physical worker deadlock | Scheduler waits park rooted continuations and return physical workers; nonresumable adapter occupancy is explicit. Cleanup/control service is independent of ordinary pool availability. |
| N3 Late originals versus authority | Foundation observation endpoints retain old-attempt bytes/cleanup/reconciliation while denying new old-generation effects or current business/context publication. |
| N4 Detached owner custody | Session execution host opens RetainedState/raw grant inside its own process. Same-process RawRetainedTransfer applies only among host threads; an existing UI-owned session settles/closes/reopens unless a separately specified cross-process handoff exists. |
| N5 Lua suspension lifetime | Registry-rooted coroutines, owned native continuations and single-owner foundation resume trampolines; nonyieldable regions and selected-runtime qualification are explicit. |
| N6 Observation versus scope close | Awaiting result/settlement is observational. Scope closure/drain is separately admitted with owner authority. |

Layer-two native recheck resolved N1/N2/N3/N5/N6 and the independent-host part of
N4, then supplied one exact residual correction: process-bound RawRetainedTransfer
must not be described as an IPC custody transfer. Root applied that correction
and the F1 clarification. No third review layer was started; the final text follows
the reviewer's supplied remedy. Its actual implementation still needs its oracle.

## Durability

Read [attack](review-durability.md) and [focused recheck](recheck-durability.md).

| Finding | Final design behavior |
| --- | --- |
| D1 Cancellation before release | Release CAS checks current scope/run/epoch, pause/cancel, manifest, publication eligibility and resource/lease grants. Recovery restores plans, never unconditional launch permission. |
| D2 Referenced bytes | Required state/source/input/result bytes reach their actual retained boundary before dependent commit/dispatch, remain live roots, and are checked on reopen. Raw metric emission keeps its separate approved no-per-emission-disk-wait scope. |
| D3 Cumulative source ACK | Coverage sets/ranges are declared. Cumulative ACK advances only over a fully retained contiguous prefix or explicit supported gap policy; out-of-order accepted input stays separately retained. |
| D4 Deadline order | Pinned owner-acceptance deadline profile with retained sequence; identity only breaks actual ties. Delayed timer callbacks do not change the cutoff. |
| D5 Replay completeness | Match the entire required historical frontier exactly once before fresh dispatch or completion; omitted/duplicate/trailing/incompatible commands block or require explicit migration. |
| D6 Late-generation observation | Same remedy as N3; old attempt settlement is separate from current publication authority. |
| D7 Actual force execution | Cache bypass/coalescing/fresh execution are distinct. Force execution of eligible repeatable work creates an actual fresh attempt; downstream irreversible actions remain separately admitted. |

Layer-two durability recheck reports all seven findings resolved in the proposed
contracts, with no residual text repair. The direct crash/cursor/replay/original
cases are future implementation obligations, not executed test results.

## Expressiveness and lifecycle

Read [attack](review-expressiveness.md) and [focused recheck](recheck-expressiveness.md).

| Finding | Final design behavior |
| --- | --- |
| F1 Old-demand publication | Input/context/node/result/demand/epoch eligibility tuple; invalidation revokes current authority before replacement admission. Late outputs retain old lineage and cannot qualify current joins automatically. |
| F2 Open membership | Producer membership/closure is independent of observing joins. Early selection freezes its own result; late membership and loser actions require explicit policy. Final/impossible outcomes account for closure/finite bounds and result revisions. |
| F3 Authored application state | Typed revisioned cells and read/write sets commit atomically with decisions/commands/context in the existing ledger. Fork modes distinguish as-of/current-at-fork/fresh/shared ownership. |
| F4 Recovery identity | Ordinary durable recovery keeps logical actor/run/node/invocation/inbox identity and changes execution incarnation. Stop-incarnation, actor retirement/tombstone and explicit successor replacement are distinct. |
| F5 Resource lifetime | Runnable slots, request execution permits, live-effect custody/cleanup and explicit leases have distinct lifetime metadata. Suspension cannot release protection still used by a writing child. |
| F6 Authoring illustration | Complete proposed Lua recorded-command function covers nested call, keyed expansion, typed quorum outcome, explicit loser control, checked proposal publication and local settlement. |

Layer-two expressiveness recheck reports F1–F5 resolved and F6 supplied, with no
additional required repair within scope. No repeated competitor research or third
assurance layer was performed.

## Remaining implementation risks

Actual same-ledger multi-command reduction, execution-host startup/lease semantics,
rooted async Lua, provider/tool facade, protected capture/cleanup, source/result
retention, live-frontier saved-state recovery and artifact validity must be built
and checked in their owning units. Design review supplies contracts and failure
cases. It does not select dependencies, establish a performance result, preserve
arbitrary heaps, qualify cross-host takeover or authorize main integration.
