# Callable Lua workflows (W1)

This is one session registry over existing audited turns, **not** a parallel or
durable scheduler. New sessions include `ultracode`: `/ultracode fix the parser` or
`ultracode fix the parser` anywhere in ordinary submitted text selects a bounded
64-step coding turn. It adds source-grounded coding/check instructions to retained
context, then runs the usual request/tool loop. Those instructions stay in context
until normally edited/compacted; invocation does not undo context or effects.

## Register or modify

Use the model `program_config` tool or `/lua` to inspect current config, edit a
copy, and submit a complete proposal with its base revision. Files can be authored
with ordinary file tools and their bytes copied into source; editing a file alone
does not change the retained registry. Model and Lua use the same tool APIs.

```lua
local current = blackbird.call('program_config', {})
local config = current.effective
config.workflow_prefix = 'wf-' -- '' gives /research instead
config.workflows = {
  {
    name = 'research', description = 'Source-grounded research procedure',
    aliases = {'research', 'study'}, bare = true,
    powerwords = {{token = 'deepstudy', color = 'heading'}},
    source = [[
      -- This is a Lua function body. args is the invocation object.
      blackbird.append({{role='developer', content=
        'Research: read targeted sources; distinguish evidence from inference.'}})
      return {arguments=args.arguments, prompt=args.prompt}
    ]],
  },
}
return blackbird.call('program_config', {base=current.revision, proposal=config})
```

`/wf-research`, `/wf-study`, and (explicitly registered) `research`/`study` select
that definition. Slash names use prefix; bare names do not. Arguments are the
**exact suffix starting with the first whitespace**, including spaces/tabs/newlines.
Operator invocation also passes `prompt` (the full original submission), `trigger`,
and `origin='operator'`. Full operator text is retained as a user context entry,
not replaced by extracted arguments. Explicit slash/bare selection wins over
powerwords. Unknown slash commands never activate embedded powerwords.

Inside a workflow or `/lua`, `blackbird.call` returns whole results, so the recipe
above reads `current.effective` and keeps `current.revision` as its base however large
the configuration is. Only a result that reaches the model is shaped (T1): the
model's own `program_config` or `workflow_invoke` call, and a workflow's drained
continuation, over 16384 bytes of text come back as head, marker with `result_ref`,
and tail ([economical defaults](ECONOMICAL_DEFAULTS.md)).

`blackbird.call('workflow_registry', {})` returns the effective revision, prefix,
and retained definitions/source. `blackbird.call('workflow_invoke',
{name='research', arguments='...', prompt='...'})` selects by canonical name.
The same tools are available to models. Workflow source is compiled as a function
body with `args`, in a scoped Lua runtime, using existing audited effects/context.

**Provider call ordering:** an invocation while provider tool calls are unanswered
returns `status='accepted'`; execution is sequential in this same turn after the
caller appends all tool outputs, before its next provider request or successful
boundary. Its completion/result is appended to context and audited separately.
Otherwise invocation is synchronous and returns the Lua result. No durable queue,
background runner, replay, or parallel execution is implied. A caller that leaves
tool outputs unanswered fails the turn instead of dispatching the continuation.
At most eight waiting continuations, eight nested executions, and 64 registered
executions per outer turn are admitted. Failures/cancellation discard remaining
continuations and prevent pending configuration activation; effects already run
remain in audit and are not rolled back.

## Matching and presentation

Powerwords are **case-sensitive exact ASCII identifier tokens** (letters, digits,
underscore), anywhere in submitted operator text, including quoted/code text.
ASCII punctuation/whitespace separates words; non-ASCII bytes are word constituents
so `éultracode`, `ultracodeé`, `notultracode`, and `ULTRACODE` do not match.
Repeated tokens or different tokens selecting the same definition invoke once.
Tokens selecting different definitions in one submission produce a `conflict`
without executing either. Assistant/tool text never triggers selection. Matching
is not derived from color or a rendered/escaped prompt.

Colors use the existing terminal palette: `keyword`, `heading`, `success`, or
`failure`. Composer and submitted user cells use these inks, including across
line wrapping; assistant/tool text receives no powerword highlighting. Registry
snapshots and terminal command catalogs are immutable/cached; no filesystem scan,
Lua evaluation, or source compilation occurs on composer keystrokes. Existing
cell-delta painting is retained. Old rendered transcript rows are not forcibly
reflowed on registry changes; subsequent rendering/resizing uses current colors.

## Activation, identity, limits

Proposals compile without running candidate code and activate only after the
successful affected outer turn. During the turn, discovery/invocation still sees
old definitions. Failed turns preserve effective configuration. Successful edits
persist in the existing audited configuration boundary and restore on session
reopen. Every selected execution retains full source, definition, configuration
revision, invocation, generation, and effect attempt identity in audit.

Definitions require exactly the six documented fields. Bounds: 32 definitions,
8 aliases and 8 powerwords per definition, 64-byte identifier/token names,
256-byte printable description, 16KiB source, 64KiB complete program proposal.
Duplicate names, ambiguous aliases/bare names, built-in command collisions, and
duplicate powerwords reject registration. Omitted `workflows`/`workflow_prefix`
in a complete proposal means empty registry/empty prefix (legacy three-field
proposals remain valid); copy current config to preserve registrations.

Direct checks: `cmake --build build/release --target workflows_test` and
`ctest --test-dir build/release -R '^workflows$' --output-on-failure` use a fake
provider and actual Lua/audit/terminal cells, not paid inference.
