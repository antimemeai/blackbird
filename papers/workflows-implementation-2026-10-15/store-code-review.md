# WorkflowStore code attack

2026-10-15. Root independent source attack of the store lane and direct native/
injected-storage oracle, before the source checkpoint. No third assurance layer.

The state candidate stages changed nodes, receipt and return data before append;
publication consumes node handles without allocation. The acknowledged flag covers
only the actual submitted batch, and catch publication checks the target revision
before adopting it again. Canonical requests compare complete native input and
fetch exact previous event payloads rather than retaining duplicate histories.
Each cell write requires a checked revision; all native events share the actual
physical batch with the delta. Prepared node addresses survive insertion.

The direct first build exposed missing physical acknowledgement output span;
the owner repaired it before green component execution. Every batch now stages
its complete output span using the actual journal environment and uses actual
acknowledged record0. Store checks now pass, including injected allocation failure
immediately after synchronization and unacknowledged committed batch reopen.

Integration exposed a separate identity-credit gap: a protected terminal batch
could still need an ordinary issuer refill for its program-record ID. Root adds
an optional already-reserved ApplicationRecordId to transact; Runtime reserves it
before effect release. The existing default remains ordinary ID allocation.
The settlement path delegates to RetainedState's restricted program-record plus
terminal-observation batch; admissions/opens/invocations cannot consume it.
Direct retained-state byte-pressure cases cover this new physical path.

Deliberate first-unit bounds: resident receipt metadata (default65,536) has no
silent eviction; full-history reconstruction is the fallback until owning saved
state maintenance registers the reducer. State-machine version changes require a
future explicit migration. No bounded-history reopen or automatic receipt archive
claim is made by these files. Native query cost remains the existing selected
index/archive mode's cost.
