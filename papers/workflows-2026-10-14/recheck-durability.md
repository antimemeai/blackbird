# Layer-two focused durability recheck — 2026-10-14

Rechecked only remedies D1–D7 from
[the layer-one attack](review-durability.md), their direct cases in
`docs/CONCURRENT_DURABLE_WORKFLOWS.md`, and affected F4/F8 delivery entries.
No broader new attack, source/test qualification or product/design edits.

| Finding | Result | Remedy checked |
|---|---|---|
| D1: pre-release recovery launches cancelled work | Resolved in design | Release rechecks current run/scope revision, owner epoch, cancel/pause, manifest, publication eligibility and actual grants. Recovery preserves the logical invocation, proves an unambiguous unreleased frontier, closes the old attempt as verified-not-dispatched and records a successor; only that successor can pass current release guards. Torn/missing/ambiguous history remains unresolved. |
| D2: references authorize effects before required bytes are retained | Resolved in design | Required source/state/input/result references identify complete bytes at the actual retention boundary before dependent semantic visibility or eligibility. Same-transaction storage is preferred; separate stores require their own completion observation. Live retention roots survive waits/replay/reconciliation/migration; reopen validates completeness and blocks missing originals. The rule explicitly preserves the approved raw contract without adding per-emission disk waits. |
| D3: cumulative ACK skips an earlier unretained event | Resolved in design | Adapter declares ACK coverage. Per-item, cumulative contiguous prefix and batched coverage are distinguished; source-supported deliberate gaps require retained policy/observations. Out-of-order acceptance stays separate from ACK frontier; offset 8 cannot acknowledge unretained 7. ACK request/outcome uncertainty remains retained. |
| D4: late timer callback changes deadline eligibility | Resolved in design | Default profile uses owner-accepted monotonic time against the wait deadline, retaining acceptance order. Post-deadline results cannot qualify because the timer callback was delayed. Event-race cutoff is an explicit alternative; batching preserves assigned order. Timer recovery retains its named wall-clock policy and never compares unrelated incarnation-monotonic values. |
| D5: keyed replay misses removed/duplicate/trailing commands | Resolved in design | Ordered/partially ordered frontier is retained and completely matched before fresh dispatch or successful return. Missing, duplicate, incompatible and order-sensitive reordered commands reject. Checkpoints root pending handles; omitted-command retirement/transfer requires the migration obligation map. A/B→A/C and A-only are named direct cases. |
| D6: stale-generation guard discards useful late originals | Resolved in design | Stable foundation endpoint retains old-attempt originals and cleanup/reconciliation observations using their capture/settlement resources. Current owner can settle the original attempt without invoking old Lua/tools or publishing into current context/demand; unmatched identities retain refusal plus observed bytes. Activation explicitly separates old capture from current publication authority. |
| D7: force-execute returns cache or coalesced work | Resolved in design | Cache bypass, in-flight coalescing and fresh invocation are separate controls. `force_execute` requires a genuinely fresh eligible repeatable producer attempt/new submission identity, never cache/in-flight joining; requested mode/actual producer are retained. Repeating effectful work follows its action policy, and forced building does not authorize downstream publication. |

The direct-oracle section names the original failure histories. F4 explicitly
owns cancellation-before-release, proven-unreleased successor attempts and complete
referenced-byte retention. F8 explicitly owns actual fresh execution. Their checks
remain implementation obligations, not evidence obtained from this document review.

No residual D1–D7 text repair is required. The design now states the missing
contracts; implementation must preserve them at the actual retained/dispatch
boundaries. This concludes the single focused recheck; no third assurance layer.
