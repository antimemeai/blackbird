# Focused authored-event design/plan attack

Reviewed the authored subplan against `CodingEngine::Runtime` in coding.cpp,
AuditLog::retain_program in context.cpp and the **exact5.4.8** Lua source archive.
This attacks generation/source ownership, descriptions/admission and C++/Lua
error/lifetime boundaries. It does not reopen the settled raw transport design.
The inspected Lua archive and primary-source hashes/restoration are in
[the acquisition report](2026-10-11-resource-sampler-references.md).

## Decisions and findings

**Lua emission returns zero values.** The bridge must not turn a valid u64 lane
ordinal into a signed Lua result or constrain lane life to INT64_MAX. Native
typed callers retain the native u64 Result. Neither success return formatting
nor a post-publication error is needed.

**R1: exact source binding requires a concrete owner path.** Existing generation_
names the root turn: turn() issues it and retains workflow bytes before Runtime
construction. Runtime::invoke_tool constructs another wrapper and calls eval()
in the same VM; the installed module function calls Lua load() on returned source.
A root binding alone does not describe all contributing source bytes.

The parent selects one **immutable VM-definition generation namespace** for
local_site. It does not select dynamic attribution to the defining closure or
nearest loaded source. System-supplied root/tool/module source loads must all bind
to their exact retained originals **before execution**. Callers share the
VM-generation local-site namespace, including nested tools/modules. Dynamic
Lua-created code has no promised static source meaning; its generation/site/value
bytes still emit. Do not imply that each event's local_site is module-local or
that a nearest-source lookup defines its meaning.

The parent will specify the concrete retention/binding API and each owner path
before source. SourceReference must come from actual successful retention, not a
cursor guessed after execution. Direct cases execute root and two nested sources
sharing a local-site value, change generation, and fail source retention. Verify
all originals and prior binding records against the selected namespace contract,
not an invented automatic per-module distinction. This remains a concrete
implementation prerequisite; the namespace choice itself is settled.

**R2: finalizer admission must remain owned through lua_close.** Runtime's
destructor calls lua_close(). Exact Lua5.4.8 lgc.c::GCTM disables debug hooks during
__gc, then converts errors to warnings and continues pending finalizers. The
existing instruction-count interrupt_hook therefore does not interrupt this work.

The parent's selected design permits the ordinary authored admission path during
lua_close with the **same borrowed owner RawWaitControl**, alive until close
completes. The emitter independently checks sticky cancellation before acquiring
metric arguments/types/body; it cannot depend on the disabled Lua debug hook.
VM END credit covers END only. It does not reserve unbounded finalizer bodies or
per-entry refusals and does not make generic Lua cleanup finite.

The acquisition boundary matters: failure to obtain control credit returns/raises
the original interrupted error **before any metric argument/type/body acquisition**.
No per-call rejection event is claimed for that unadmitted call. The held owner
END records admission-closed/cancellation. Later finalizer calls see sticky
cancellation before argument acquisition, without waiting for refusal capacity.
Once a call obtains its control claim, every acquired argument rejection or
emission outcome uses that claim. This capability does not claim an audit event
for every Lua function entry; the broader nls10 acquisition/audit work is separate.

A full-lane fixture must cancel actual lua_close finalizer admission independently
of the debug hook, include multiple finalizers, and check published prior bodies
and the held owner END. Distinguish admitted invalid arguments (rejection is
published) from canceled unadmitted calls (no metric/body acquired, no invented
per-attempt event). Check that canceled calls do not wait for fresh refusal credit.
No Lua error jump may cross a live RawControlToken or other C++ owner.

Current Runtime installs no lua_setwarnf. Lua5.4.8 may therefore discard the
warning callback entirely when a finalizer raises. Warning delivery is optional;
it cannot be the preservation path for cancellation or admitted rejection.
The direct fixture must exercise warning unavailable and warning installed cases
without treating warning delivery as publication. Pure user code looping forever
remains an existing VM-lifecycle limitation; this capability does not solve it by
adding a VM END or a fabricated finalizer count/time limit.

## Sound parts and source requirements

Strict native extraction and typed scalar/byte bodies fit the emission rule.
Use `lua_type(...) == LUA_TSTRING`, not lua_isstring: the latter also accepts
numbers and lua_tolstring can then allocate/convert. Integer extraction requires
lua_isinteger, with number/boolean likewise exact native type checks. These APIs
remain nonallocating on the already validated scalar variants. Borrowed string
bytes are copied immediately after full size/admission with no Lua call or yield
between check and publication. Immutable setup descriptions must include Lua
integer/number representation, boolean int, size_t string length, source/VM Ref
layout and all rejection variants before activation.

C++ failure handling must destroy native claims/owners before any allocating Lua
error push or lua_error longjmp. A fixed native error literal is sufficient after
that scope; no formatted metric/error body belongs in emission. Admission errors
before a call obtains its control token stop metric acquisition and are reflected
in the held owner END, as above; acquired rejections have per-call credit. Do not
fence an ordinary interruption.

Source retention success yielding its actual SourceReference is preferable to
inventing locators. The parent owns the concrete binding API and implementation
after the integrated checkpoint; this report implements no authored source. The
selected contracts above resolve the design choices. Their owner paths and direct
oracles must accompany source; another broad transport review or a review of this
review is unnecessary.
