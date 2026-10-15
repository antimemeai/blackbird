# Native TUI input targets

Exercise actual Composer, text/grid painter and SceneHost with at most8192 input
bytes. Start with the ordinary AFL forkserver so every input gets a fresh native
process; persistent-state performance is not claimed. There are two command-line
modes, independent AFL corpus/output directories and at most two low-priority
workers. No TerminalUI run loop, external editor, Lua evaluation, provider, shell,
filesystem effect or network request is invoked by target input.

Composer mode feeds arbitrary bytes plus explicit timeout boundaries. Check cursor
bounds, expired non-paste recovery/idempotence, saved-state roundtrip and actual
rendered grid tail/width/continuation invariants across variable small geometries
and overwrites. Scene mode varies publication/reorder/removal, invalid candidates,
draft compare-and-swap, selection and deliberately armed target consumption.
Check exact accepted results and refusal preserving prior snapshot/local state,
plus immutable snapshot independence from later caller candidate mutation.

Sanitizers independently detect memory/undefined faults. Fixed seeds execute the
same native paths normally for replay/source checks, while AFL input mutations
run only on Neuroses. Input sizes/geometries/operation counts are target limits,
not product admission or correctness claims. Use finite initial execution counts,
retain every unique crash/hang with source/tool identity, replay before calling
it confirmed, and queue ordinary bugs without product fixes in this collection
unit. One independent target/runner challenge then root recheck; no new tribunal.

Latest operator steering supersedes active findings work in this unit: launch AFL
through Python, confirm initial startup and then leave it alone. Its native
crash/hang/queue outputs remain the untriaged inbox for collective work. No case
inspection, ongoing polling, minimization or product repair during this campaign.
The native target challenge finds one armed-state oracle gap, corrected before
launch with input-derived expected transitions and fixed reorder/removal cases.
