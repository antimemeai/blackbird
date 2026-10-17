# C++ / Lua presentation contract from the first instrument

A module returns a plain table containing `compose` and at most8 named handlers.
Its immutable original source is at most64KiB. Each invocation gets a fresh VM;
globals/upvalues are scratch, not persistent application state. Native selection,
scroll, pins and the Composer remain native. An owned request carries actual
scope, view/source/request/scene/input/query stamps. A stale result cannot publish.
The native adopter admits only the expected `results` collection and `comparison`
inspector for this application. Other applications may choose other typed scenes.

`ui.rows()`, `ui.key(i)`, `ui.label(i)` read at most8 frozen native rows.
`ui.detail(1|2)` and `ui.detail_title(1|2)` expose two pinned4096-byte originals
and their separate identity/extent descriptions. Lua strings may contain NUL,
terminal controls or arbitrary bytes; constructors sanitize presentation and retain
full bounded originals. `ui.collection(key,title)` returns a native builder index.
`ui.item(builder,key,label,input_index)` copies the actual frozen input target;
its authored item key is only presentation identity. `ui.inspector(key,title,text)`
constructs literal safe lines. No draft mutation interface is exposed.

`ui.request(verb)` is legal only inside an identified named handler, never module
load, compose or preview. It accepts a finite native intent against the handler's
frozen target and opaque native Trigger. It is not a domain-success return. A
caught constructor/phase error is still sticky native failure. Already accepted
intents survive later Lua failure. Up to8 pending envelopes remain in the
independent inbox; identical retained Trigger/key redelivery returns its original
local acceptance even after native handoff, changed arguments conflict, new
triggers are distinct. No token can be reconstructed from printable key bytes.

The native bridge binds actual environment/conversation and physical CommandJobs
incarnation, validates the exact target/verb and hands one immutable request to
the existing mailbox. It acknowledges the inbox only after retaining admission,
refusal or unknown. It never repeats an uncertain handoff. A recent16-row
presentation cache queries the same original native request key; retirement is
not retry authority. Existing process reservations/control/worker/syscall owners
remain authoritative. No second operation ledger or invented semantic locator.

Default VM limits are16MiB live Lua allocation and100000 instructions, checked
at1000-instruction quanta. Scene limits are4 nodes,32768 total rows/spans and
196608 owned string bytes; aggregate authored inspector input is bounded16384.
Finite native constructors add no I/O, raw capture, domain calls or blocking waits.
Restricted libraries exclude native loaders, finalizers, debug/registry access,
coroutines, patterns and unbounded native range/callback operations. Nested pcall
loops yield to the owned count hook. Stop joins the actual VM worker; no detach.

The adapter continues polling while closed and reload does not replace the native
bridge/inbox. At whole-runtime exit it joins VM then bridge and captures any
actual remaining accepted envelopes as unknown-on-runtime-exit, using constructor-
prepaid bounded DATA credit. Raw4090–4098 preserve source/pin/input/candidate/
adoption/handler/forward/exit links;4094/4096 are CONTROL, the others DATA. Existing
process records preserve actual consumption/native calls. No duration, histogram
or metric transform is computed on these producers. Exact wire fields are emitted
in `raw_ui_description()`; originals and native syscall receipts remain distinct.
