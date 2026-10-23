# Native Oxcart host tools: delivered Blackbird unit

2026-10-23. Implementation commit
`0a7e2979d7b6e298b71faa208bb5588a40f304f8`, pushed on
`work/oxcart-host-tools-2026-10-23` from master `a94d094`.
Source tree: `19912c459ef51c92bd148d9a16385005aa6e7636`.
Worktree: `context/oxcart-host-tools-2026-10-23`.

The [accepted native protocol](../docs/APPLICATION_HOST_TOOLS.md) adds a
connection-local catalog and callback to the existing model/Lua tool loop.
Catalog/native schema validation, provider-boundary schema conversion, ordinary
retained admission/results, exact original replies, catalog/operation linkage,
progress, cancel/steer/close/EOF and callback detachment are implemented. No new
library, generic MCP client, browser executor or TUI change is included.

Oxcart accepted the version-1 message/field definitions through its shared
coordination note. It owns browser targets, capture, jobs, source artifacts and
the outward MCP adapter. The native Blackbird callback does not implement a
second browser-operation owner.

## Direct results

All log paths below are relative to the worktree's ignored `context/` directory.

| Check | Result | Retained record |
| --- | --- | --- |
| New wire circuit against unchanged a94d094 implementation | RED: missing host-tool ready echo; after correcting a fixture double-join, exits cleanly with the same failure | `red-recheck.log` |
| Review: accepted result followed by malformed frame | RED before fix; success must settle before later transport failure | `red-received-frame.log`, `green-received-frame.log` |
| Review: outgoing control-frame overflow | RED before fix; local refusal must be not-started and permit a corrected call | `red-oversize.log`; final suite GREEN |
| Review: exact integral exponent/large values | RED before native Number validation fix; fractional values still reject | `red-integer.log`, `green-integer.log` |
| Full focused host-tool suite plus legacy application tests after lint repairs | 2/2 Debug, 9.59s | `green-lint-fix.log` |
| Final affected host-tool suite under Mac ASan/UBSan | 1/1, 3.60s, no sanitizer diagnostic | `green-asan-lint-fix.log` |
| Additional affected ASan application, actual CLI, coding and program-config cases | 4/4 in first scoped run; its pre-frame-fix host-tool failure retained separately | `green-asan.log` |
| First full installed source hook | 266/266 Debug, 275.76s; then three changed-unit lint failures | `source-gate.log` |
| Final full installed source hook | 266/266 Debug, 278.50s; full 258-unit owned C++ analysis/format and Lua diagnostics/contract pass | `source-gate-final.log` |
| Actual retained final-tree CLI | Native offer/ready echo, stale result rejection, close and session reopen pass; no turn/provider dispatch | `runtime-smoke.log`, `check-retained-host-tools.py` |
| Linux Release/ASan | Unavailable: Neuroses SSH timeout before upload | `linux-check.log` |

The host-tool suite runs the actual application loop with a scripted provider:
request host state, receive a retained reading locator, use ordinary `read_file`,
and continue to a source-backed final response. It also checks native/Lua name
collisions, invalid arguments before callback, original accepted/rejected/progress
packet bytes, cancellation and stale results across turns, explicit host failure/
cancelled/unknown outcomes, nested native schemas and catalog admission linkage.
This is an internal integration case, not a live browser or model-service result.

The [one independent implementation review](2026-10-23-oxcart-host-tools-review.md)
identified the three behavior defects above; all received direct REDs and fixes.
The affected recheck closes the two-layer scope. Initial Kimi access failed with
subscription HTTP403; it supplied no review. One independent Codex review replaced
it. No further review round or added assurance layer. The lint repairs use named
schema/value and reply arguments plus a stable waiter reference, with no new
suppression. The two pre-existing NOLINT sites remain unchanged.

## Runtime and remaining shared integration

Retained final executable:
`context/oxcart-host-tools-qualified-runtime-2026-10-23/build/debug/blackbird`.
Its `SOURCE_COMMIT` is the implementation commit above; `SOURCE_TREE` is the
matching tree. Oxcart's existing exported-source builder retained the exact source
archive, source directory and source metadata there. The earlier 40adde14 candidate
also remains retained separately, without being mislabeled final source.

Oxcart reports successful actual native connection negotiation using its entire
four-tool catalog with that earlier candidate, retaining its ready reply under
`context/build/blackbird-host-runtime-checks/70b75a3b-4cd0-46ed-aad4-7f3900e00947/ready.bplist`
in the Oxcart repository. That checked negotiation and closure, with no model turn.
The final staged child separately passes the actual CLI smoke above.

The Oxcart coordination note now supplies the final commit/tree/runtime and direct
results. Live panel wiring, an actual browser state/capture model turn and the
same operation through outward MCP remain Oxcart's delivery work. Source is pushed;
master is not merged, the existing mainline runtime is not replaced, and the
operator's active Blackbird process is not changed.

Operator says Dolt is unused. No Dolt repair, initialization or subsequent
Beads/Dolt operation was invoked. Existing issue backup snapshots and unrelated
work remain preserved; remaining integration work is recorded here and in the
journal rather than treating unavailable issue storage as a delivery gate.
