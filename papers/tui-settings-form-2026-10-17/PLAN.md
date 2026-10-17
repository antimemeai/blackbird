# First reusable form: session model and effort defaults

2026-10-17. Proposed next Unit3 slice, source-grounded during the preceding
navigation tranche's required gate. No product implementation, dependency
adoption, new review of that tranche or qualification follows from this note.

## Purpose and exact scope

An operator opens Settings without disturbing a partly written prompt, reads
which model/effort defaults are saved and which override supplies the next
request, edits the two session defaults, applies them after current work, and
returns to the same prompt. An invalid field, stale edit or storage failure
leaves both the effective settings and the operator's input intelligible.

Deliver one native two-field form and the narrow settings-owner contract it
needs. Keep provider/account and workflow selection read-only context. Do not
bundle sign-in, model discovery, account changes, Context editing, team work or
all program definitions into this slice. A Settings route joins the shared
native view picker through a named presentation action.

## What the harness actually supplies

| Inspected source | Current behavior and limit |
| --- | --- |
| [SessionSettings / SessionStore](../../include/blackbird/session.hpp) | Model, effort, name, workflow and provider are one settings value. `settings()` returns an owner-borrowed reference; `save()` replaces a validated whole value. There is no read revision, guarded field patch, field error result or thread-safe observation mailbox. |
| [Session validation / persistence](../../src/session.cpp), `validate`, constructor and `save` | Saves changed settings as audited `session.settings`; assigns resident settings after the record. Identical save is a no-op. Reopen restores audited settings. Model is nonempty, at most1024 bytes and excludes NUL; effort is low/medium/high/xhigh/max. OpenRouter requires a slash in the model. Name/provider/workflow have separate rules. These rules are actual validation, not a qualified model capability list. |
| [Main dispatch](../../src/main.cpp), `/model`, `/effort`, `/session`, `validate_route` | `/model` saves the new session model then calls `engine.model`; `/effort` validates the current route, saves, then calls `engine.effort`. `/session` reports launch defaults and provider/workflow identities, not a form version or complete override explanation. Registered portable Responses restricts effort to low/high/max. Route validation reads provider metadata and belongs off the UI thread. |
| [Terminal worker](../../src/terminal.cpp), `start` and `perform` worker dispatch | Submitted commands queue behind current work. `perform` runs on the serialized worker with actual retained-state handoff; input and painting stay on the terminal owner. A form cannot call `SessionStore` or engine setters directly from key handling. The current queue holds prompt strings, not typed form proposals. |
| [CodingEngine](../../include/blackbird/coding.hpp), model/effort setters | These setters mutate owner state; they are not concurrent control mailboxes. The effort setter validates its enum spelling. |
| [Program configuration](../../src/coding.cpp), `program_config`, `request_impl` | A different owner already exposes effective/pending program revisions, launch defaults and derived request defaults. Nonempty program model/effort overrides session defaults. Explicit `blackbird.request` options can override a particular request. Whole program proposals also include modules/workflows and activate at a successful workflow boundary; this is not the session-default mutation API. |
| [Native components](../../include/blackbird/tui_components.hpp) | Checked scenes, keyed nodes and revisioned draft storage exist. `DraftEdit` stores bytes/cursor but performs no editing. There is no delivered reusable form with several fields. `Composer` still combines terminal decoding, prompt editing, history and command grammar. |

`session-info.bbm` is a derived discovery snapshot, never a source for editing
active settings. A new model name is not proof that a provider supports it; no
qualified model catalog exists. Account identifiers or provider metadata must
not bring credential contents into the form, model observations or raw UI text.

The [doctrine](../../docs/TUI_DOCTRINE.md), [component contracts](../../docs/TUI_COMPONENTS.md),
[existing-view settings design](../../docs/TUI_EXISTING_VIEWS.md), and
[Unit3 plan](../../docs/TUI_IMPLEMENTATION_PLAN.md) govern this proposal.
This deliberately distinguishes the existing owners rather than relabeling
program configuration as saved session settings.

## Missing owner contract to implement first

Add a native session-default operation over the existing session/settings and
engine owner, shared by the form and existing slash adapters. It needs:

1. **Owned observation:** actual session/environment identity, a settings-owner
   incarnation and version, saved defaults, current provider/workflow context,
   and a compact read-only explanation of effective program overrides and their
   revision. Include real pending program overrides when known, clearly labeled.
   Publish immutable bounded snapshots outside input/layout/paint. Do not expose
   `SessionStore::settings()` across threads or call the full program tool read
   on every frame. The engine owns the override projection; the form does not
   infer it from old transcript text.
2. **Guarded patch:** request identity, session/owner identity, observed base
   version and only changed model/effort fields. Compare the base on the execution
   owner immediately before mutation. Construct the candidate from current
   settings so name/provider/workflow survive. Every mutation of those shared
   settings, including `/name` and `/workflow`, must update the observed version;
   identical saves need not. A process-scoped version is sufficient if restored
   drafts must reread and cannot submit against an earlier owner incarnation.
3. **Authoritative result:** applied, unchanged, stale-base, invalid-field,
   unavailable or failed, with field-specific explanations where known and an
   owned current snapshot on conflict. Validate the complete candidate and route
   before any save or engine setter. Preconstruct engine values so allocation
   failure after the audit write cannot leave old runtime defaults while reporting
   a simple unapplied failure. If a later failure occurs after observed save,
   report the saved/applied distinction honestly; do not automatically retry.
4. **Serialized timing:** receive a typed proposal through a finite mailbox;
   service it through the existing worker/storage handoff after active work
   concludes, before its intended subsequent request. Idle application uses that
   worker too. Do not turn Apply into two queued `/model` and `/effort` strings:
   they could partially apply, lose one common base, or become conversation input.
   Queue admission is shown as waiting, not saved/effective. Cancellation or
   shutdown withdraws an undispatched proposal explicitly and retains its draft;
   reopening must not replay an old Apply automatically.

This is an owner addition, not merely rendering two fields. Extract only the
necessary typed worker item path alongside current prompt work. Do not add a
second execution thread that races the engine, or service persistence on the UI
owner's idle callback. Keep original request/result capture and retained-state
handoff semantics. Existing `/model` and `/effort` must reach the same owner
validation/patch path, with their present serialized timing.

Expose the same guarded native operation to human and structured Lua/model
adapters rather than leaving the form as sole mutation authority. A thin bounded
read/patch adapter is part of this owner slice; adding a complete settings/tool
registry or a new external protocol is not. Existing `program_config` retains
its separate definition and activation semantics.

## Reusable component and visible form

Build an owned `FormState` keyed by field IDs, independent of Blackbird settings.
It keeps read base, local field draft/revision, byte cursor, dirty fields,
validation messages, active field and pending submission identity. A publication
may update surrounding observations without overwriting newer native edits.
Discard/reload is explicit. Navigation away and back preserves compatible drafts.

The first controls are a single-line text field, an enumerated choice field,
Apply, and Discard changes. Use the shared display/byte-column helpers and native
editing semantics; extract the needed literal text-editor primitive from
Composer rather than feeding settings through prompt history, slash completion
or submit grammar. Keep the existing prompt editor as a consumer of that primitive
where extraction is necessary, without redesigning submitted command grammar.
Paste edits the focused text field literally; Enter in a field is not Apply.
Use explicit focusable Apply/Discard actions and publish their effective controls.
Provider/account information stays read-only and contains no credential bytes.

Suggested rows: session identity, saved model, saved effort, read-only provider,
effective request defaults with source, pending program change if any, field
errors or conflict, Apply/Discard. A program override explains why changing a
saved default may not change request behavior. Do not label the selected default
as the model of an already-running attempt. Individual explicit request options
remain an observation of that attempt, not a global setting.

Local editing/focus stays native while applying, streaming, composing Lua or
waiting for stdout. A submitted revision is immutable. If the operator types
after Apply, its result settles that earlier submission without clearing or
replacing the newer draft. Form closing does not cancel admitted work silently.

## Direct REDs and acceptance

Before source, extend an actual-main local fixture to open Settings through the
view picker and require the two-field form; unchanged main must fail that visible
expectation. Then use direct owner/component tests before their implementation.

| Claim | Direct oracle |
| --- | --- |
| Pure editing | Open Settings with a Unicode prompt and interior cursor, edit model/effort, leave and return, then cancel form edits. Exact prompt bytes/cursor and form draft survive navigation; provider/effect ledgers stay empty. |
| Atomic guarded change | Submit both fields from baseA; independently change settings to baseB before dispatch. Require conflict, unchanged current settings and retained user fields. Retry deliberately from the returned base; require one audited save and both fields applied while name/provider/workflow survive. |
| Field errors | Empty/oversized/NUL model, unsupported effort and OpenRouter model without slash return the matching field error. Portable Responses rejects medium/xhigh before save. No partial default mutation. Do not mistake unknown model support for successful remote qualification. |
| Activation boundary | Gate a local fake provider request under defaultsA, submit defaultsB while it is running, and inspect captured request bodies. Current request remainsA; proposal visibly waits; after completion the next eligible request usesB. Applying alone issues no provider request. |
| Override truth | Retain a nonempty program override. Change saved defaults and require the form shows saved valuesB while effective request defaults remain the override with its real revision. An explicit request option is independently observed in the actual request. |
| No lost edits | Submit draft revisionN, edit toN+1 before the result, deliver out-of-order observations and the result forN. Newer field bytes remain; result attaches toN. Stale owner/session incarnations refuse dispatch. |
| Storage failure and recovery | Reject save before its audited record: settings/runtime unchanged and draft retained. Inject failure after observed save: result preserves that observation and never automatically resubmits. Reopen restores the actual audited defaults and no pending Apply replays. |
| Input service | With real blocked stdout and a delayed owner result, edit/navigate/return and request cancellation through existing native paths before draining. Form paste containing route/cancel bytes does not become an action. Private-PTY controls remain with the child. |
| Presentation | Microsoft Ghostty/Alacritty actual-main checks cover form focus, field errors, conflict, queued/applied status, resize and prompt return. Reviewed readable wide/narrow expectations cover the same concrete field meanings and clipping. |

Use local fake provider/request ledgers and owner IDs, not live accounts. Add
relevant native/raw/persistence/sanitizer/Linux checks and the required full source
gate; perform one independent code review and its direct fixes within the existing
two-layer bound. Run the normal matched Linux Release tranche comparison using
the previous exact source and unchanged observer; new form episodes establish
their own baseline. No additional renderer investigation, AFL activity, artwork
inspection, account adoption or running-executable replacement belongs here.

This slice is complete when one useful form and one guarded settings operation
work across native human and structured adapters with truthful source/timing.
The reusable form then supports task/session fields and the Context/team work
already sequenced in Unit3; those domains retain their own owners and plans.
