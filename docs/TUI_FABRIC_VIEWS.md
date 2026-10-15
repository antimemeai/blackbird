# Capability and action fabric: concrete surfaces

2026-10-15. Proposed views and contracts derived from the
[research](../papers/tui-program-2026-10-15/fabric/REPORT.md) and [interaction theory](INTERACTION_THEORY.md).
Wireframes use illustrative data and names. They are state sketches, not existing
screens or fixed keybindings. Proposed operation names below describe required
semantics; only APIs explicitly marked existing are callable today.

## Shared components and navigation

Use a **Collection** with stable object identity, filtering, explicit scope and
selectable columns; an **Inspector** with summary/source/parameters/bindings/history;
a **Parameter Form** with typed values and field errors; a **Source Editor** with
external-editor continuation; a **Revision Comparison**; and **Operation History**
with effect/original links. Reuse these across tools, skills, endpoints and hooks.
Do not build one bespoke settings application per vendor.

Collection focus follows identity during asynchronous updates. New rows do not
move the selected action. Search is local over the loaded catalog; remote search
or Refresh has a separate progress/result state. Narrow terminals show a reduced
row and open the same inspector full-width; the previous filter, selection, draft
and scroll return intact. Details omitted for width remain reachable. Help names
the active bindings and target; no key letters below are reserved by this design.

The command palette searches action names as well as objects: "edit start-turn
hooks", "invoke release lookup", "show effective tools", "open integrations".
Selecting an object opens it. Selecting an explicit Invoke/Run verb executes it
according to its current contract; ordinary invocation adds no confirmation page.
Every inspector supports Copy reference, Open source, View definition changes,
and Find uses where those records exist. These are ordinary view actions.

An inspector's compact header separates **source/configuration**, **availability**,
**runtime** and **last attempt**. A package may be installed and disabled; a
connection may be configured and unreachable; a capability may be discoverable
and unexposed to the current request; a result may be failed while the adapter is
healthy. One green dot cannot express all four.

## 1. Capabilities collection and inspector

Episode: find the right capability across local definitions, packages and remote
servers, inspect why it is available, and use or revise it without losing the work.

```text
CAPABILITIES                 Scope project:blackbird + session:main
Find release                 Kinds all    Availability any
Name                 Kind        Source               Effective  Last use
> release.lookup     API recipe  project/release.lua    r12        succeeded
  github.releases    MCP tool    github endpoint A      catalog r7 never
  release.review     Skill       personal/prompts       r4         prepared
  ultracode          Workflow    session registry       r9         running

release.lookup                        API recipe / project / r12
Looks up release metadata for a repository.
Target service api.github.com   Account public   Method GET
Available here: yes    Exposed to current model request: no
Inputs: owner, repository, tag       Result: retained response
Run / Edit / Save as skill / Bind / Inspect uses
```

The inspector discloses qualified identity, description, input/result contract,
source and contributor scopes, effective/pending revision, supported verbs and
actual local/remote owner. "Exposed to model" identifies the request tool set or
discovery mechanism observed; it does not infer exposure from installation.
Selecting why/unavailable opens a concrete reason: account missing, scope filtered,
schema unsupported, source invalid, adapter unavailable or definition shadowed.

Model projection: bounded list/filter by kind/scope/availability, inspect a selected
identity with source reference and action schemas, then invoke its qualified
operation. Lua receives owned native values and ordinary operation references.
Existing workflow/tool registries can seed these projections. The cross-kind
catalog and model exposure query require a new catalog owner; no claim that all
current definitions already have these fields.

## 2. Packages, loaded mods and contribution graph

Episode: determine what an optional package contributes, enable its selected
components, and understand what changed after reload.

```text
PACKAGES                       Installed / configured / runtime
Name             Source           Scope    Enabled  Runtime
> project-feed   local@src42       project  yes      gen18 active
  midi-controls  archive@pin8      personal no       not loaded

project-feed / contribution map
Package source src42  Config cfg19  Effective gen18  Pending gen19
Tools 2   Skills 1   Views 1   Hooks 2   Connections 1
Pending: hook refresh changed; waiting for workflow w71 to conclude
Runtime gen18: timer t9, subscription s4; 1 active invocation
Open components / Edit config / Enable / Disable / View changes
```

Package source, installed version, enabled selection and loaded instances are
separate objects. The graph is a navigable list of contributions and dependencies,
not necessarily a node animation. Each component opens the usual inspector. A
failed component names its error and any effects/resources already created;
partial load does not appear as a successful whole package. Discovery reads
metadata, not executable package top levels.

Disable's result states future admission stopped and identifies running work or
cleanup still pending. Reload creates a candidate generation without running its
effects during validation; existing affected work retains its old meaning.
Unrelated packages/colleagues continue. No generic Restart all action belongs here.
Deleting local package source and deactivating a runtime are different actions.
Source removal cannot erase historical invocation references.

Backend: existing trusted workflow-local `packages.lua` can support its limited
status/feed uses, but persistent inventory, contribution registry, generation
custody and generic mod lifecycle require owned native support. Model/Lua need
the same contribution and runtime queries, not instructions to parse this screen.

## 3. MCP servers and their three catalogs

Episode: configure a client endpoint, discover its actual features in the chosen
account context, inspect a tool/prompt/resource, and use the correct operation.

```text
MCP ENDPOINT github-work / project
Transport HTTP    Endpoint https://example.invalid/mcp
Protocol requested 2026-07-28   Last accepted 2026-07-28
Account work-gh    Availability observed 10:41:02 / now stale
Catalog tools 24 loaded, more pages / prompts 3 / resources unsupported
Changes listener interrupted    Last refresh failed: HTTP 503
Last usable catalog r7 retained; current availability unknown
Refresh / Edit / Account / Tools / Prompts / Resources / Attempts
```

Endpoint creation form takes a local connection identity, scope, transport and
endpoint or process configuration, supported protocol profile and credential
reference. It displays which process, if any, Blackbird owns as a client child.
Saving does not launch it; Connect/Refresh is an explicit effectful operation.
For modern HTTP, "connection" is the user's client configuration/availability
concept, not a fictitious protocol session. Older protocol adapters identify
their actual initialization state separately.

Tools catalog opens Parameter Form and Run. Prompts catalog opens arguments and
Resolve, yielding a message preview; Insert in draft, Include in context and
Start work are separate choices. Resources catalog includes direct URIs and
templates; Read creates a retained observation, Attach selects a particular
captured result or a clearly specified refresh-on-use policy. Template arguments
are encoded according to the adapter contract. Catalog Refresh does not read
every resource or resolve every prompt.

List pagination, last successful revision, authorization context and incomplete
results remain visible. A list-change notification marks the relevant catalog
outdated and schedules refresh under its policy; it does not silently dispatch
tools. An unavailable capability differs from an empty successful catalog.
Unsupported schema features produce an explicit form limitation or adapter
error; do not coerce nested MCP inputs into the current scalar Lua-tool schema.

Backend required: versioned MCP client adapters/catalog capture, account-context
association and source-result/original references. Existing native operation
ownership can host their attempts once implemented. Source documentation is not
a shipped client. External JSON is parsed at this adapter boundary directly to
owned native values; the internal catalog/configuration is not a JSON database.

## 4. Execution workbench and reusable API recipes

Episode: make one well-specified invocation, inspect partial failure, then reuse
the useful parameterization without accidentally replaying an effect.

```text
EXECUTE release.lookup@r12             project:blackbird / local host
owner          [ patrickbeam                  ]
repository     [ blackbird                    ]
tag            [ latest                       ]
account        [ public                       ]
Resolved request: GET /repos/patrickbeam/blackbird/releases/latest
Defaults from project r12; tag overridden in this draft
Run / Save values / Save as skill / Show source

Attempt op348   recipe r12   input snapshot i31   status completed
Transport HTTP200   Adapter result parsed   External effect read observed
Result summary / Original bytes / Compare prior / Open referenced item
```

The form identifies required/default/current values, type constraints, scope and
environment. Field errors preserve all entered values. Object/list fields use
expandable typed editors where supported; large payloads and source use the
shared editor. Sensitive inputs display references/status rather than credential
contents. No secret token is copied into a saved recipe, conversation or ordinary
history. Account setup uses the actual provider's private path.

Run resolves exact capability and input revision at admission and returns an
attempt reference. Result summary is distinct from original content. Pending,
needs input, failed validation, protocol failure, tool execution error,
cancellation requested, settled cancellation and unknown effect have their
actual meanings. Supported ongoing attempts offer Observe/Wait/Stop. Stop's
availability and actual disposition come from the adapter, not an ornamental
button. Additional server input resumes the identified operation according to
its protocol, without silently starting an unrelated attempt.

A saved API recipe contains endpoint/operation, named typed parameters, defaults,
result projection, account reference and source revision. Map values directly
through the adapter's path/query/header/body encoding; don't construct an
unquoted shell command from form strings. HTTP is one kind, not the universal
internal action model. A native tool, Lua workflow or MCP call can also be the
recipe's operation. Direct HTTP recipe transport/registry is proposed backend
work; existing Lua tools can express narrower recipes through audited operations.

Save as skill creates a reusable named instruction/procedure or parameterized
invocation according to the selected kind; it does not run it. Reopen last input
loads a new draft. Run again deliberately creates a new attempt under a disclosed
definition revision. An unknown prior effect is shown beside it, with explicit
context for the participant's decision; the UI never claims retry is rollback.
Comparing attempts preserves separate input/result/source references.

Model/Lua access: inspect schema and defaults, supply native parameter values,
invoke, then observe by attempt reference; save the parameterized definition
separately. Current `blackbird.call` and workflow/tool registries supply useful
owned paths; proposed generic recipe operations must be named unavailable until
their registry/adapter owner exists.

## 5. Skill library: prompts, procedures and supporting material

Episode: retain a useful method and find/invoke/revise it in another appropriate
episode. The operator's terminology governs: skills include reusable prompts,
not only directories holding a standardized `SKILL.md` file.

```text
SKILLS                        Scope all / Kind prompt + procedure + recipe
Name               Kind        From            Invocation
> source-review    Prompt      personal r4      prepare draft
  research-paper   Procedure   project r8       include instructions
  release.lookup   Recipe      project r12      execute operation

source-review / r4
When useful: inspect a concrete source change with explicit claims
Arguments: source reference required; focus default correctness
Preview with current args / Insert draft / Start work / Edit / Bind
Used by: key mapping review; workflow ultracode; 12 retained invocations
```

Metadata discovery shows concise purpose, kind, arguments, source and scope.
Opening a skill obtains its complete retained body and relevant supporting
references on demand. Importers preserve external format and provenance without
making one vendor's layout the product definition. Alias collisions display both
qualified entries and explain the conflict; a display name alone is not dispatch
authority.

Prompt preview shows the resolved text/messages and omitted/missing substitutions.
Instructions included in model context retain skill revision and insertion
identity. An executable recipe/workflow uses ordinary attempt semantics. A
procedure's instructions can describe programs without executing those programs
merely because the user opens it. If a skill has multiple invocation choices,
the default is named visibly. Do not force a preview every time a fluent user
invokes a known command; preview remains available as its own action.

Backend: current callable workflows plus retained source/context operations cover
some cases. A first-class skill catalog, named typed arguments, import formats,
cross-scope references and usage indexing need their own owner. Missing indexing
should show usage unavailable, not a fabricated zero.

## 6. Hooks and bindings: the instrument for changing instruments

Episode: add, understand or revise how an input/event causes useful work,
including intentional prompt/context contributions.

```text
BINDINGS                  Scope project:blackbird   Event turn.start
Name               Source        Effective  Pending     Last dispatch
> project-guidance project r41    gen41      gen42       succeeded t507
  notify-blocked   personal r8    gen8       none        no match

project-guidance / edit candidate gen42
Event       turn.start      Target participant:self / project:blackbird
Selection   ordinary user or addressed work; exclude this binding's descendants
Order       before request assembly; after project context source
Action      append instruction contribution from skill project-guidance@r6
Failure     fail this start with visible error; preserve already captured effects
Save and activate / Inspect changes / Explain match / Open Lua source
```

Bindings collection filters by event, target, source scope, enabled state and
conflict/error. It includes keyboard/controller mappings by link to the shared
input mapping surface; device-specific semantics stay in that owner. Hook kinds
are explicit: observer, context/argument transform, operation action, admission
decision or continuation request. A callback cannot pretend to be every kind.
The form only offers event/result contracts the backend supports.

Event selector names timing and accessible inputs, not just a string. Target
selector can pin an object identity or declare a supported resolution rule such
as focused task at input acceptance. Rules display precedence/contributors and
the targets a sample event resolves. Pure declarative Explain match evaluates
selection without dispatching an action. Arbitrary Lua predicates are not called
"dry run" unless the owner genuinely supplies an effect-free evaluation boundary.

Action/source selection can choose an existing operation, skill or Lua body.
Argument form binds fields to constants or specific event values. The source
editor retains unsaved and saved candidates across view changes. Handler order
is visible and validated for the event family: transforms may compose in order;
observers may be independent where declared; a post-effect failure cannot undo
the effect or cause automatic re-execution. Invalid order/dependency cycles and
ambiguous target mappings identify the exact conflicting definitions.

### Start-turn hook authoring without restarting

1. The operator asks the model to add project guidance at turn start. The model
   inspects effective program/binding definitions and their revision, writes the
   Lua or instruction source, and submits its configuration through ordinary
   retained operations. It can also write an editable source file and explicitly
   publish those bytes; a file-save notification alone is not publication.
2. Save and activate validates declarative shape/source without running candidate
   effects, retains it with base revision, and identifies the candidate generation.
   Existing `program_config` proposals are complete snapshots, so the editor/model
   copies all preserved fields. Stale-base failure keeps the proposed change and
   exposes the current source for reconciliation.
3. While the author's affected turn runs, the inspector shows `saved gen42 / active
   gen41 / pending gen42; waiting for t508`. That turn continues under gen41. After
   its successful boundary, gen42 becomes effective for new eligible roots. Idle
   configuration authoring uses the local publication owner and does not launch
   a model request or require a process restart.
4. The next eligible turn pins gen42, records its start event and selected handler
   list, performs the context contribution once at the specified boundary, then
   assembles its request. History links event→binding generation→contribution→
   context revision→actual provider request. No retrospective claim that t508
   already ran the newly authored hook.
5. A load failure names its source location and keeps gen41 effective. A hook that
   fails at execution names the affected start, partial effects and configured
   continuation disposition. Its result is visible to both operator and model.
   A retry is an explicit new attempt with known/unknown prior consequences.

The start-turn binding backend is missing; this is its minimum observable behavior,
not a claim that the existing registry already dispatches hooks. It can reuse
retained program snapshots and audited context operations. Do not expose an
immediate-apply button while its atomic interrupt/settle semantics are missing.
Normal Save and activate is routine agency; it does not ask the operator to
approve the model's source at a new gate.

### Dispatch history and lifecycle failures

```text
DISPATCH d93 / source event e77 / root turn t509
Matched project-guidance@gen42  Input snapshot ev77
Action op551    Contribution c102    Request req88
Observed completed
Related: no duplicate admission; no remaining callbacks
Original event / Handler source / Input and result / Follow contribution
```

History is populated by requested finite pages, not continuously scanning the
whole audit. Each row identifies match, skipped-with-reason, rejected admission,
running action or result. It does not record "success" at attempted dispatch.
Repeated delivery of e77 can refer to the existing admission; two genuine controller
presses have distinct event identities. Reentrancy/cycle handling reports the
chain and configured limit reached; no silent loop or silently lost pending work.

Reload retires old handlers for new dispatch while retaining references needed
by already admitted operations. Callbacks that try to initiate new work using
retired state receive a stale-generation result according to the owner contract.
Timers/watchers/subscriptions have explicit cleanup; pending or failed cleanup
stays visible. Killing a view is not authorization to kill a shared service.

## 7. Integrations, accounts and known infrastructure

Episode: understand which service/account a capability will use, repair a missing
connection, and consume existing infrastructure without assuming control of it.

```text
INTEGRATIONS
Name        Capabilities      Account          Availability
GitHub      releases/issues   work-gh          last request succeeded
Papers      topic feed        public           observation stale

KNOWN INFRASTRUCTURE
Name        Kind       Environment   Client state       Owner
research-db database   lab-host       observed reachable external fabric
kernel-a    kernel     compute-host   not attached       external service

research-db
Reference infra:rdb7   Endpoint reference lab/research-db
Consumers: release.lookup, paper-index
Last observation 10:33   Current state unknown until queried
Inspect connection / Observe / Open available client operations / Forget locally
```

Integration detail links optional package, account selection, endpoint, contributed
capabilities, pending config and attempts. Account detail shows provider/account
identity as supported, auth method, available status, last observed failure and
private setup/switch/disconnect operations. No credential values in the general
inspector. Existing `/auth` provider paths remain usable; new external account
types need real adapters rather than a universal login form.

Infrastructure entries name where the service lives, who governs it, observed
versus configured availability and the client operations actually exposed. Attach,
query or disconnect affects this consumer. Provision/scale/restart shared service
is absent unless a separately authorized external operation really exists; local
inventory is not its control plane. Forget locally removes this reference's future
use and preserves historical attempts; it does not delete the remote database.

Model/Lua projection supplies selected account/endpoint references and client
operation schemas without secret contents. A current observation is an explicit
operation; merely opening this view does not poll every service. Persistent
inventory and integration-account association are missing owners; existing
provider-auth status supplies only its delivered provider slice.

## 8. State, continuity and acceptance examples

The reusable state presenter accepts facts, not a single total enum: draft/source
saved; validation result; pending/effective generation; observed availability and
age; in-flight operation; last outcome. It uses concise dominant status with
expandable distinctions. A long-running old generation and a new active definition
can both be true. Selection, parameters and unsaved source stay anchored while
those facts update.

| Episode | Required observable result |
| --- | --- |
| Add hook during current turn | Saved/pending/effective distinguished; current turn retains old list; next eligible turn uses new list without native restart. |
| Invalid Lua or concurrent definition edit | Draft retained with source/base conflict; old effective configuration still inspectable and usable. |
| Inspect MCP prompt, then leave | Resolved observation retained if fetched; no unrequested context insertion, model turn or tool execution. |
| Catalog changes while form open | Form retains values and selected identity; changed schema/revision produces a disclosed rebind or correction before dispatch. |
| Run saved API recipe after unknown prior outcome | New attempt is explicit, previous uncertainty visible, no automatic replay on opening or refresh. |
| Mod fails after external action finishes | The action's observed result remains linked; hook failure is separate and never turns into an implicit repeat. |
| Duplicate event or self-trigger chain | Existing admission reference or explicit supported-cycle refusal; distinct intentional events remain usable. |
| Change account/scope while editing | Prospective context is disclosed; admission binds actual selected identity or returns stale selection. No silent credential fallback. |
| Disable package while its view is focused | Draft/reference survives; future invocations unavailable for a stated reason; running work/cleanup retains its actual status. |
| Inspect infrastructure or metrics | No service governance, raw-capture transforms, SDK startup or remote polling arises merely from rendering. |

These examples identify direct future checks and unresolved owner contracts.
They do not imply those backends have been implemented or launch an evaluation
campaign. This lane designs the working surfaces and the minimum truthful queries
and operations those surfaces need; the larger program selects implementation
sequence and component boundaries.
