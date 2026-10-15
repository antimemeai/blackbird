# Economical observations: independent design/plan review

One independent review lane,2026-10-15. Reviewer reads actual tools, coding,
default Lua, audit and indexed retained-source code against
[the unit plan](../docs/ECONOMICAL_DEFAULTS.md). No product edits by reviewer.

All findings accepted before source implementation:

1. Canonical file-range normalization drops unrelated fields. Parse the output
   budget from original arguments, not the normalized range. Assert canonical and
   legacy range plus limit, with omission relative to selected bytes.
2. The current schema requires path. Make snapshot-only calls expressible and
   enforce exactly one of path/snapshot, including wrong types and nulls.
3. Capture the actual returned SourceReference; do not derive a sequence or reread
   a path. Use strict journal identity and positive exact integer sequence. Assert
   default observations at the next real provider request, explicit zero/large
   budgets and canonical/legacy ranges, full custom Lua reads, exact omitted
   middle-fact recovery after replacement/unlink/reopen, stable reference and
   invalid-ref refusal. Original and displayed byte counts remain distinct.
4. Invalid-range correction guidance must retain a snapshot input, rather than
   suggesting a fresh path read during repair.

The reviewer finds no design blocker after these clarifications. The existing
direct source index supports recovery without a transcript scan. Code/oracle
review remains in this same lane after implementation; no additional panel.

## Single code/oracle review and finding-driven correction

The reviewer inspects the implementation and direct fixtures. No production
blocker is found. The initial fixture lacks several promised direct cases:
canonical line/legacy range plus limit, large explicit workflow budgets/ranges,
binary and split-UTF8 counts, malformed/missing/extra/null source references,
snapshot-preserving error guidance and reference identity after reopen.

All requested oracles are added. The reviewer checks those corrections and the
small native validation/trace changes only, and reports no remaining blocker.
Successful actual next-provider observations now show default and explicit limits;
the exact omitted middle constraint is recovered after replacement/unlink/reopen;
one command effect is retained. Temporary diagnostic prints are removed. The
fixture's reopen setup now follows existing confirm/recover lifecycle calls.
This completes the one review lane; no additional assurance round is requested.
