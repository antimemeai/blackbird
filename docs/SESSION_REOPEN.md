# Session reopen

Ordinary reopening restores context, pending context proposals, settings and
conversation identity. It never resubmits a prior operation merely because the
process ended. Drafts load separately and require Enter before submission.
Reopening requires `--session DIRECTORY`. Without it, startup creates a fresh
private conversation and prints its path; no shared default or old Arconaut
directory is selected. `--list-sessions` finds saved conversations. Inspection,
recovery, continuation and expiry flags require an explicit session path.

| Session state | Reopen behavior |
| --- | --- |
| Settled single-segment session with usable saved state/catalog | Restore saved semantic state and scan the accepted suffix. Archived payloads stay on disk and are loaded by explicit queries. |
| Missing, damaged, or unsupported derived state; audit at most 64 MiB | Announce recovery and replay the authoritative audit automatically. A successful supported rebuild writes new derived state. |
| Same cases with audit over 64 MiB | Stop before full replay. `--rebuild-session` explicitly allows the slow path. |
| Station history, pending restart, or an unresolved operation | Full recovery under the same size rule. Station scheduling resumes only with an explicit station/resume invocation. A pending turn continues only with explicit `--resume-continue` or `--resume-once`. |
| Unfinished provider request | Validate its linkage, record unknown outcome, accept no prior response and replay no request. Further work may proceed. |
| Linked native file/command/Lua/workflow attempt | Ordinary reopen refuses. Explicit `--recover-unknown-effects` validates the whole unresolved set and atomically records UNKNOWN without replay. Opened command/process attempts retain a custody warning across saved-state reopen. |
| Unfinished operation with unknown or invalid custody/linkage | Refuse new work; show an inspection/fresh-session instruction. `--rebuild-session` does not waive uncertainty. |
| Linked audit predecessor | Refuse with a migration message. This launcher does not reopen multisegment histories. |
| Damaged authoritative journal or exhausted recovery capacity | Existing journal corruption/capacity handling stops admission. Rebuilding derived state does not bypass this fence. |

Full recovery is bounded by the launcher's existing 512 MiB / 200000-record
capacity. This is a work/storage limit, not a time promise: disk and host scheduling
can make it slow. Compact reopening also has to scan its uncheckpointed suffix.
An audit larger than 64 MiB may still reopen automatically through valid saved
state; the threshold controls full replay, not session size or resident memory.

`--rebuild-session` allows full recovery; it does not discard valid saved state,
force a replay when a compact path is available, repair authoritative corruption,
resolve uncertain effects or select a destructive reset. To inspect a large session
that needs replay, use `--audit-last --rebuild-session`. To begin independently,
select a new directory with `--session`. Keep the old directory for inspection or
an explicit migration.

The 64 MiB automatic fallback bound is intentionally finite. Small legacy sessions
retain convenient recovery while large histories cannot silently turn an ordinary
launch into a complete historical replay.

`--recover-unknown-effects` acknowledges uncertainty for supported native local
attempts; it does not recover a child handle or establish that an old command
exited. A durable `uncontained_exec_recovery` warning in `/stats` blocks restart
and automatic backstop for that session. A fresh independent session remains
available. File-only recovery and unopened command admissions do not claim an
uncontained process. Invalid linkage, unsupported historical payloads and custom
named Lua operations remain fenced; the flag does not convert historical formats.
