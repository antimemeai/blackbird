# Slash command modernization

Source: the 2026-10-19 slash assessment and survey (absolute corpus paths in
operator request), terminal registry/composer and main dispatch. BLACKBIRD.md is
a dangling ../BLACKBIRD.md symlink in this worktree; read the primary repository's
/Users/patrickbeam/projects/blackbird/BLACKBIRD.md instead, without modifying it.

## Units
1. Completion and honest names: expose exact-prefix alternatives; /status with
   /session alias; /clear-screen with explicit /clear compatibility notice.
2. Friendly participant arguments and model-authored /compact, preserving JSON.
3. Everyday commands: retained transcript export/copy, known model discovery,
   working-tree Git diff (not speculative file-effect rollback), side question,
   project initialization.
4. Saved markdown prompts: deterministic discovery into callable workflow registry,
   literal $ARGUMENTS substitution, no shell interpolation.
5. Only after 1–4 are committed, consider rewind with a separate effect plan.

## Checks and bound
For changed existing behavior add direct regression tests, execute against old
implementation and record observed failures here before implementation. Build with
scripts/rigor build debug, run targeted terminal/workflow/PTY checks. Each source
commit uses the laptop hook lock and full pre-commit gate. No mutation, install,
restart, push, beads or other-branch edits.
Hardening has two layers only: implement/review/fix concrete faults, then one
recheck/fix. Resource allowance: targeted tests per unit plus required hook gates;
no unrelated suite runs or third assurance layer. Fault classes: ambiguous command
completion; mistaken display/context semantics; malformed positional arguments;
context/tool linkage loss; clipboard control injection; transcript omission;
provider side effects/retries; prompt collisions and unsafe substitution.

## Results

Unit 1 red: new recalled-exact-prefix terminal test failed against unmodified
implementation: `terminal contract at line 239` (slash list stayed closed).
Implementation opens candidates on ambiguous fallback Tab, adds /status and
/clear-screen with compatibility aliases and an explicit history-preserved notice.
Unit 1 green: scripts/rigor build debug completed; terminal_test passed;
check-terminal-pty --case palette passed (search/cancel/draft recovery/busy/narrow
resize). Layer one caught ChatView assignment misuse at compile time and adjusted
to clear/append. One recheck updated the /stat ranking expectation for new /status;
terminal + palette checks passed. No third review.
Unit 1 gate findings: workflows_test's exact alias expectation needed the same
candidate-list update (research also prefixes research-alias). Installation
fixture inherited BLACKBIRD_EXECUTABLE pointing to the installed executable;
subsequent gates unset BLACKBIRD_EXECUTABLE/ARCO_EXECUTABLE rather than changing
unrelated installation code. The first gate was interrupted by the continuation
boundary; no commit was created. Recheck fixes stay within the declared layer.
Gate environment follow-up: BLACKBIRD_SPRITE_ASSET also leaked the installed
resource path into the install fixture. The direct mock install transaction check
passes with all three executable/resource overrides unset. Stopped the already
failed gate; verified its process exited and Git HEAD remains 168bb26 before a
corrected gate attempt. No installed Blackbird was changed.
