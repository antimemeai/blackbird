# Delivery checks

2026-10-15. Documentation and research delivery only.

Sources: [coordination](coordination/sources.tsv), [fabric](fabric/MANIFEST.tsv),
[internal baseline](INTERNAL_SOURCES.tsv). One integrated design challenge and
its fixes are recorded in [resolution](review/RESOLUTION.md).

Direct file/hash/ignore/link checks and staged whitespace checks are recorded below.
No product implementation, runtime tests, evaluator, UI/device trial or benchmark
result is claimed. No acquired implementation was executed.

Verification passes29 original-source byte-length/SHA-256 identities:
28 newly retained originals (10,980,122bytes), plus one reused shared Codex
archive. All new originals are ignored regular files with no nested Git metadata,
symlinks or filesystem detritus. Shared archive resides in the workspace's
quarantine_proj rather than requiring a nonexistent workspace Git ignore.

All20 internal source identities match their recorded Git baseline.
All287 local links across28 checked Markdown files resolve.
Live-source restoration commands remain provenance records; restoration was not
rerun to replace captured bytes with current website responses.

Root's one consistency recheck confirms the five challenge fixes in canonical
documents: phase-gated effects, acquisition-pinned coordinate targets, per-invocation
identity/ambiguous delivery, literal insertion versus deliberate POWERWORDS, and
a named Context/request/repair delivery slice. No runtime behavior is claimed
verified by this documentation check.

Staged `git diff --cached --check` passes. The staged delivery contains only
documentation/research catalogs and supported beads backup state; no product
C++/Lua source or credentials. The operator's unrelated artwork remains untracked
and untouched. Full C++/Lua tests/lint are outside this text-only change.
