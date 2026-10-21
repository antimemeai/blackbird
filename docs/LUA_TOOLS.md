# Live Lua-defined tools

A model or operator can define a small tool during ordinary operation, without
rebuilding Arco or adopting a dependency. Schema and Lua source are session-owned
configuration; Lua globals/closures are not persisted.

## Define, finish the workflow, then use

The model tool is `tool_define({definition: {...}, base?: REVISION})`. Lua also
has `arco.define_tool(definition)`, a convenience for the same retained call.
`arco.call('tool_registry', {})` returns the effective revision, effective
complete definitions, pending complete definitions and pending revision. Called from
a program these results are whole (T1).

A definition may carry `shape = {output_max_bytes = N, output_head_bytes = H}`: when
the tool answers the model's own call, a result whose text (a string as is,
otherwise its JSON) exceeds N bytes comes back as its first H bytes, a marker with
`result_ref`, and its last N - H bytes; N alone keeps a head of N bytes; H alone
makes the budget the larger of 16384 and H + 12288, so the tail is 16384 - H for a
head under 4096, else 12288. Without `shape` the default is 16384 bytes, 4096 head
and 12288 tail; budgets are at most 16777216 text bytes. A result whose cut would not
be shorter than its text comes back whole. The tool's own arguments are
never read for this, even ones named `output_max_bytes`. Called from a program, the
tool's result is whole.

```lua
local result = arco.define_tool({
  name = 'write_note',
  description = 'Write a maintenance note through audited native I/O',
  parameters = {
    type = 'object',
    properties = {
      path = {type = 'string'},
      content = {type = 'string'},
    },
    required = arco.array({'path', 'content'}),
    additionalProperties = false,
  },
  source = [[
    return arco.call('write_file', {path=args.path, content=args.content})
  ]],
})
assert(result.staged)
-- End this workflow successfully. Pending definitions are not callable yet.
```

After that boundary, ordinary `arco.request()` exposes `write_note` alongside
native tools, and `arco.call('write_note', {path=..., content=...})` dispatches it.
Explicit request `tools` overrides still deliberately replace the default list.
Definitions survive native restart/session reopen. Each turn still has a fresh VM.
No transparent retry or old-attempt authority is inherited.

A definition's `source` is a **Lua function body** with argument `args`, returning
a JSON-encodable value. Syntax validation compiles but does not execute it. Effects
occur only on invocation. The body has ordinary Lua/arco APIs, not raw io/os/package.
This is not a hostile-code sandbox: ordinary Lua can change globals and request
models. Use `arco.call` for native I/O, as with other workflow code.

## Validation and boundaries

The first slice deliberately supports a small schema, not arbitrary JSON Schema:

- Root: `type='object'`, `properties`, `required` array (use `arco.array({})` for an
  empty Lua array), and `additionalProperties=false`; no other root keywords.
- Property: scalar `type` (`string`, `number`, `integer`, `boolean`, `null`) and
  optional string `description`. No nested objects/arrays, enums or constraints.
- `integer` accepts integer JSON **lexemes**, not fractional/exponent spellings
  such as `1.0` or `1e0`. Exact numeric lexemes otherwise remain preserved.
- Required properties must exist; duplicate required names, unknown properties,
  unsupported keywords/types and wrong argument types reject before tool effects.
- Definition keys are exactly `name`, `description`, `parameters`, `source`.
  Names: 1–64 ASCII letters/digits/underscore/hyphen; native names and `provider`
  are protected. A valid existing Lua name stages a replacement.
- At most 64 properties, 32 tools, 16 KiB source per tool, 4 KiB description,
  64 KiB serialized registry. Capacity refusal can stop the workflow; it never
  publishes the oversized candidate.

Missing/wrong fields, unsupported schema and Lua syntax errors return a retained
error result. Effective configuration stays unchanged. A failed or interrupted
workflow discards all its pending definitions. Valid definitions staged together
publish at one successful boundary, alongside managed context/byte-policy changes.
An optional `base` binds the effective registry revision; omission explicitly uses
the current one. Multiple definitions within a workflow compose the pending list.
A pending revision is not dispatch authority.

## Retained effects and limits

Definition input/schema/body, effective boundary configuration, invocation input,
invoked source/revision and operation result are retained. Native calls inside the
body use the same audit/effect-policy seam as direct calls. The existing 16-level
nested operation bound applies to recursive tools. Cancellation and capacity stops
remain native workflow stops.

A wrapped `exec` timeout still returns `timed_out=true` and
`effect_outcome='unknown'`; inspect its retained `output_ref` rather than replaying.
Local child cleanup does not reconcile remote effects. An author can transform a
returned value, so a tool description or custom result is not a native assurance.
No general durable Lua state, full schema engine, named-module/configuration
platform or multi-provider orchestration is claimed by this slice.

## Actual use

G3's actual OpenAI model defined `g3_source_map` in one ordinary request. After a
successful workflow boundary, a later request in the same reopened session saw and
invoked it. It read the three native registry sources through retained `read_file`
and wrote a 35-line-match implementation index. Original session and output remain
at `context/giga-campaign/g3/actual` and
`context/giga-campaign/g3/live-tool-source-map.md`. This map is a maintenance aid,
not a semantic call graph or correctness receipt. See the delivery note for checks
and activation status.
