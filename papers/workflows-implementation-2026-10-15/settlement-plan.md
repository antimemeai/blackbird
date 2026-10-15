# Atomic task settlement and bounded results

The initial integration attack exposes two missing invariants: running tasks have
no protected space for terminal state, and cancellation is not retained first.
Before enabling the CLI, extend the existing settlement credit to a deliberately
restricted batch: one program record followed by terminal observations, with no
new admissions, opens or invocations. WorkflowStore selects this path explicitly;
exact duplicate transactions still cost zero. Existing append validation and
acknowledged publication remain unchanged. Direct checks must exhaust ordinary
headroom while the pre-reserved atomic task result still commits; a settlement
batch containing an admission must refuse before append.

The native task facade bounds input to 512 KiB and its captured originals to
1 MiB per file/process. LocalTools keeps its existing default for other callers;
the facade selects a smaller owned collection capacity. File replacement size is
checked before writing; process overflow stops collection and retains the actual
partial bytes. An encoded result has a 4 MiB allowance (including original bytes
and the current tool presentation). Reserve that allowance plus framing per live
task before admission. New work cannot consume that space. Keep the reservation
through worker shutdown and owner drain. Raw metric transport retains its existing
independent byte-preservation contract; this semantic journal credit does not
claim to replace it.

Retain cancel/shutdown control intent before requesting stop. A failed terminal
append stops new admission, leaves executor completion bytes drainable, and keeps
old attempts uncertain on reopen. Allocation or process failure during an effect
must never become success or permission to replay. Full station custody across a
host crash remains a later delivery unit, not an assertion made by this facade.
