# Integration proposal 1: work/clm-rd7 → master

This proposal is for the operator to decide (plan §5.3). Nothing merges to master by implication. If it is
accepted, master gains the a1v branch's context-management units and four context-lifecycle units: S0, S0b,
P1a and P1b. It also gives 5st's rework the profile mechanism on master to build on (plan §1.4).

Source: `work/clm-rd7` at `7ee04da` (the P1b merge). There are 23 non-merge commits since master `a94d094`, across
115 files.

## What changes for a running Blackbird

**Context and the model**
- **The context budget is on by default:** 384 KiB nudge, 192 KiB target. That last value is retired later,
  in E1. A session that never chose a policy follows the default.
- **Every request carries a `CONTEXT_SIZE` line** that informs and never nudges. On Anthropic it appears only
  when the trigger is reached.
- **The TUI shows a blinking `◆ CONTEXT` badge** when the context is published by edit, restore or management.
- **Managed context keeps a model's response round whole.**

**Tool output, shaped at creation**
- Every program gets shaped tool observations by default. `exec`, `process foreground` and unranged
  `read_process_output` return 8 KiB (2 KiB head and 6 KiB tail, with `next_read`). Unranged `read_file`
  returns 16 KiB.
- Explicit ranges and limits stay exact, as do the operator's own `/exec`, `/process` and `/fg`.
- Decision records name the keys Blackbird supplied (`native_defaults`).

**Stopping and reopening**
- **Snapshots on every clean stop.** Every clean exit writes a snapshot, so a clean reopen replays nothing.
  Clean exits include orderly errors on a quiescent journal.
- **Reopen record.** A reopen that finds frames after the snapshot writes one `session.reopen-suffix` record
  and shows a neutral notice.
- **SIGTERM is a clean stop**, with status 143. A second SIGTERM within 2 s is coalesced, and a later one
  kills.
- **The wrapper forwards SIGTERM** and restores default SIGINT and SIGQUIT in its child.
- **A SIGTERM'd station** records a supervisor stop and is not paused.
- **The raw recorder** obeys only its supervisor's SIGTERM.

**Provider profiles**
- Profiles ship with Blackbird. They are data-only Lua tables, read by a native reader that executes nothing.
- They load once at session start, and the loaded set is recorded.
- Every `provider.request` records its profile and model, plus a binding digest.
- A missing or invalid profile set refuses startup visibly.
- Nothing reads the behavioural rows yet: provider behaviour is unchanged.

**Other fixes**
- File raw events for real file-tool dispatch (4by).
- Clipped participant metadata is marked (5z4).
- End follows inspector growth (tnn).
- The startup benchmark names its binary (k9r).

## Downgrade

An older binary still opens a session written by this one. That is by design: no test has exercised
it, so it is a claim from reading, not an observation.
- The new records (`session.reopen-suffix`, `provider-profile-set-v1`, `station.supervisor-stop`) sit on
  channels or labels an old binary ignores.
- Snapshot slots keep format version 1. S1 changes that later.

## Evidence

Each unit passed one review and one recheck (BOUNDED_HARDENING). Each unit's tests failed on the old code,
recorded in the journal and in `context/clm-rd7-notes/`. Every unit and merge commit passed the full Mac
gate, and the touched suites passed on Linux debug and ASan.

## Known and tracked, not fixed here

| Bead | Issue |
| --- | --- |
| `arconaut-yc4` | contexts over about 8 MiB cannot be snapshotted (pre-existing; owned by S1) |
| `arconaut-mze` | plain-mode Ctrl-C kills the raw supervisor and recorder (pre-existing) |
| `arconaut-0ad` | an unexplained slower `/effort` return after a clean reopen |
| `arconaut-r5n` | `tui_retained_result` is intermittent with no load |
| `arconaut-8zr` | Linux parallel tests time out under shared load |

## Decision

Accept the proposal, and master receives a `--no-ff` merge of `work/clm-rd7`, committed through the hook.
5st then branches from master. Decline it, and 5st and campaign-defects keep waiting (plan §1.4), while this
program continues on `work/clm-rd7`.
