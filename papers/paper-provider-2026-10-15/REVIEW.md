# Independent review and corrections

The installed kimi-colleague workflow was attempted once. Kimi Code returned
HTTP 403: the current subscription has no Kimi Code access. No review was
completed by Kimi; the raw failure is under ignored context/kimi-review/.
A Codex colleague performed the required independent design and code challenges.

The design review accepted the bounded approach and required: explicit portable
handling in colleague routing; errors, incomplete tools, duplicate call IDs and
malformed arguments rejected before dispatch; descriptor/protocol resolution
before saving settings; no silent effort remapping; and pinned account binding
through every retry. These constraints are implemented and directly exercised.

The one code challenge found that interactive /effort could save medium/xhigh in
a portable session, making the next request and ordinary reopen fail. The shared
main-route validation now runs before both launch and interactive effort saves.
The native fixture rejects both invalid interactive efforts, verifies max is
retained, and reopens without repair flags.

The colleague also identified the existing shared successor-seed validator's
requirement for encrypted reasoning. This delivery documents that precise
limitation for Paper plaintext reasoning rather than changing context seed
contracts. Ordinary tool loops and session replay/reopen remain in scope.

No further concrete defect was identified in account binding, normalization or
terminal response validation. This is the single code challenge/recheck, not a
claim of universal correctness. No third review campaign is added.

The first live test exposed Paper's documented automatic strict-tool schema
normalization: without an explicit strict choice, optional native parameters
became mandatory and read_file could not satisfy its own validator. Portable
export now sets strict:false only on function definitions without an authored
strict field, preserving the exact parameter schema and explicit strict choices.
Direct export and native fake-transport checks cover the actual wire fields.

The later operator-requested fresh-start repair received a focused code challenge.
It found that maintenance flags could still select an older published UI through
the shell wrapper. The shell now refuses pathless maintenance before selection
and routes explicit maintenance to the native executable. The startup fixture
checks both the actual launch paths and published-generation selection; the Debug
recheck passes. Native allocation, explicit reopen and restart retention had no
further concrete finding. This repair's plan and results are in FRESH_START.md.
