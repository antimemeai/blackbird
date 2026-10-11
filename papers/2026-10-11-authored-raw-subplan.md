# Native and Lua authored raw events

2026-10-11. Completion unit for the authored capability in census rows22/59
and the Lua bridge portion of systemA. It supplies emission, rather than a
metric registry, consumer, evaluator or callback. Other program/workflow owner
sites remain separate work. Study grounding is the retained older Unix
ktrace/kdump source and raw-emission literature: acquire native arguments, keep
separate record types, preserve first and interpret elsewhere. Lua's installed
lua.h exposes strict type tests, integer/number extraction and exact string
pointer/length. The existing Runtime protects allocating Lua calls with pcall
and keeps C++ owning locals out of Lua error jumps; retain that discipline.

## Interface and descriptions

Expose `blackbird.emit(local_site, value)` (the retained `arco` alias refers to
the same function). Require exactly two arguments. local_site must be an actual
positive Lua integer, not a coerced number or numeric string. Value must be an
actual Lua integer, number, boolean or byte string. Tables, userdata, functions,
threads and nil are refused. Do not call tostring, traverse tables, construct
Value objects, encode BBM/JSON, call a subscriber or calculate a metric.

Reserve static sites1500–1599 for this owner. Four fixed sites specify integer,
real, boolean and byte-string bodies.
Each body copies generation[16], actual VM execution Ref(lane16,ordinal8),
native signed64 local_site, and the selected native scalar; a byte string has
size_t length and exact bytes. Lua boolean uses the int returned by its native
API. Compile-time profile guards require a signed64 Lua integer and a native
double Lua number; persist those widths/representations in setup descriptions.
No conversion into a common numeric representation. Native callers get typed
integer/real/boolean/byte overloads with the same owner and local-site fields.

The fixed body descriptions are in immutable run descriptions before capture
activation. Generation-local local_site is an uninterpreted value. Its meaning,
units and names belong to the VM generation's retained code, not a writer-side
lookup table. This is a VM-generation namespace. It does not attribute an event
to the dynamically executing closure or pretend that the root source defines
every local site. After AuditLog::retain_program has preserved the exact original,
emit a source-binding event with actual generation, journal/source reference
and source length. Have retain_program return the SourceReference it already
constructs; do not guess a locator from a later cursor. If source retention
fails, no VM starts or authored event is accepted for that generation.

A Runtime emits an execution-begin event before newstate, then native allocation/
installation return and a held execution-end event after close. Ref identifies
that actual execution, not a nearest timestamp or invented common parent. A
refused allocation has its actual null/status outcome and no created VM. Nested
evaluation in an existing VM retains that same VM identity; separately observed
source-load records copy the exact already-produced source before load, keeping
root/tool/workflow/installed-helper code distinguishable. Module-source return
also captures the existing source bytes before Lua receives them. Descriptions
identify those actual load/return boundaries, not an inferred closure origin.
Lua-created dynamic load source does not acquire a fabricated static-description
binding; its emitted values still carry the actual VM generation and local id.
Invocations belong only in events whose owner
actually has one; do not manufacture invocation ids for top-level Lua.

## Admission, lifetime and errors

An authored call first acquires one finite control claim for its refusal/return
boundary, at a safe Lua/native boundary. Strict extraction uses nonallocating
APIs only. Its complete native body size is checked before data admission and
publication. The sole execution owner does not yield or invoke Lua/metamethods
between the final capacity check and copying borrowed string bytes. No borrowed
pointer survives that immediate copy. Native typed callers obey the same rule.
Oversized indivisible authored bodies are refused; neither truncate nor silently
split one user's event. Strings carry embedded NUL and high bytes unchanged.

Successful Lua emission returns zero results. Native callers can consume the
actual native u64 ordinal. Lua's signed representation does not restrict an
otherwise valid lane ordinal, and success cannot fail during result conversion.

Every admitted rejection emits its original local-site availability, argument
count/type and Error fields from held control credit. Interrupted admission
does not create an ingest fence; held rejection/terminal credit still publishes.
Capacity/ingest errors keep the original error fields. C++ exceptions are caught
inside a scope that releases all C++ owning locals before lua_error is called.
No new Lua allocation is attempted to format the metric or error body.

VM teardown holds finite end credit from setup. Lua finalizers can execute user
code during lua_close. Authored calls use normal pre-acquisition admission with
the same borrowed owner cancellation/deadline control, whose endpoints must
outlive lua_close. They check that control independently of Lua debug hooks:
Lua disables debug hooks during finalizer execution. End credit covers only the
VM end event; it does not fund arbitrary finalizer metric bodies or promise that
arbitrary user finalizer work is finite.

Cancellation before control admission raises the original interrupted error
before acquiring metric arguments/types/bytes. No per-call refusal event is
claimed for that unadmitted acquisition. The held owner end records its observed
closed/interrupted admission outcome. After control admission, every actual
argument rejection or publication failure uses that call's held outcome credit.
Repeated finalizer calls after cancellation stop before argument acquisition;
they cannot consume a fictional infinite rejection reserve. Lua may issue a
warning for a finalizer error, but the published admitted outcome is independent
of warning delivery. Current Runtime has no installed lua_setwarnf; this unit
does not claim a complete Lua-warning audit or complete nls.10.

## Direct oracles and sequence

Write exact expected native bodies independently of the emitter. The real Lua
bridge emits INT64_MIN/MAX values where permitted, negative zero/nonfinite real
bit patterns, both boolean values, empty/binary strings and local-site ids. Root
source and a subsequent changed generation bind to their actual retained source
references; nested execution uses its actual VM ref. Invalid arity, coerced site,
nil/table values and oversized strings produce exact refusal fields without an
accepted body or table traversal. A stopped real recorder/full lane exercises
cancel/deadline with held terminal credit intact. Source retention and Lua
allocation/load errors prevent invented activation/success. Consume actual raw
disk prefixes, not a shadow callback stream. Native typed calls exercise the
same byte layouts. A red existing-Lua missing-function oracle precedes source.

The focused design/plan attack selected zero Lua results and identified source
attribution and finalizer admission as defects in the earlier draft. The actual
boundaries above replace those claims. Finish the concrete static layout and
load/return credit calculation before the direct red oracle, implementation/green, one code/
oracle attack and finding-driven fixes/recheck. Use the owned source gate and
relevant Mac/Linux profiles. Six total workers, no dependency adoption, periodic
main-process timer, eval execution or laptop mutants.
