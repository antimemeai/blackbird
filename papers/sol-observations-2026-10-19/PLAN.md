# Usable tool observations — 2026-10-19

Two conceptual units, q4n then 47k. No activation, push, beads, or ageing.
BLACKBIRD.md is absent here; read the shared /Users/patrickbeam/projects/BLACKBIRD.md.

## Representation

1. Reproduce exec's 16384-byte cut through a three-byte character before fixing
   it. Budget cuts retreat to a UTF-8 boundary and prefer a newline within 256
   bytes, without turning binary into text. Keep explicit byte slices exact
   (including hex for intentionally split characters). Report half-open absolute
   byte offsets; retained originals are untouched.
2. Default exec: 8192 raw bytes, 2048 head + 6144 tail. Half the former request
   payload; enough opening context, three times more space for final diagnostics.
   Explicit budgets remain authoritative; custom Lua remains unbounded.
   Separate output_head/output_tail with exact ranges when elided, not a made-up
   joined stream. Include output_ref, omitted range, and executable next_read
   parameters in the result. File reads stay head-first at 16384 bytes and carry
   snapshot plus exact byte continuation. Recovery adds canonical line ranges to
   read_process_output: reuse its retained-output ownership, not a second file
   identity or disk fallback. Explicit byte reads remain exact.

## Direct checks and fault classes

- q4n: actual LocalTools exec of deterministic UTF-8 straddling 16384; assert text,
  exact returned/omitted offsets and byte-exact capture. Nearby newline, binary,
  zero, explicit split byte ranges. CommandJobs projection must use the same cut.
- 47k: actual native programs/turn.lua + deterministic provider: failing build-like
  output retains trailing error; next request fetches an omitted middle line from
  output_ref, then sees its exact fact; command effect marker remains one byte.
  File continuation, explicit zero/larger limits, full custom Lua, retained reopen.
- Run each new test on old implementation first and record RED. Build with
  scripts/rigor build debug, targeted tests; required locked commit hook full gate.

## Bounded hardening

Layer one implements these faults and direct checks. Layer two is one source and
oracle recheck of clipping, offset arithmetic, UTF-8/binary distinctions, recovery
identity and linkage, with finding-driven fixes. Resource allowance: local debug
builds, targeted tests and required per-commit hook gates; release executable build
only, no restart. No third review, mutation campaign or unrelated host suites.

## q4n observations

RED on unmodified 168bb26 implementation: `tools_test` exits 1 with
`q4n: default-budget cut hex-encoded readable UTF-8`. Input is 16383 ASCII
bytes, U+2019 at [16383,16386), then `tail\n`; budget 16384.
The fixed result returns [0,16383), omits [16383,16391), and captures all 16391
bytes unchanged. Existing split-budget expectations now require text, while
explicit byte slices retain their byte-exact hex behavior. First affected run
passes command_jobs/coding/economical_defaults and exposes the old binary-prefix
expectation in tools; corrected that expectation to the new boundary contract.

q4n layer-two recheck: verified lookahead and boundary arithmetic, binary
continuations, explicit-byte exactness and capture-before-projection. Required a
valid boundary character as well as a valid prefix so arbitrary leading binary
continuation bytes cannot be mistaken for a split character. Final targeted tools
passes (0.39 s); the other three affected checks passed above. No third review.

The first full hook runs 265 tests: 252 pass, 13 fail. One is relevant:
request_storage compares returned and retained native result bytes; Lua sorts
returned object keys, so adding unsorted offset fields exposed a mismatch. Emit
file fields in lexical order; request_storage now passes unchanged. Remaining
failures are inherited test environment: BLACKBIRD_GRAPHICS/SPRITE_ASSET/EXECUTABLE
point to the installed process, and the long sandbox HOME makes tui-test's Unix
socket exceed its path limit. Unsetting those process overrides makes
terminal_blocked/install_blackbird pass; HOME=context/h (inside ignored working
and build contexts) makes tui_viewport pass. Retry the required locked gate with
that explicit test environment, not a hook override or test skip.

The second q4n gate passes 263/265; terminal_pty_tools attribution and
terminal_pty_state exit timing fail under the parallel suite. Both pass unchanged
in a direct follow-up (32.33 s). They are not observation-projection defects; do
not alter TUI folding in this task. One final required q4n commit submission uses
the corrected environment and the already checked source. No hook bypass, test
suppression, unrelated repair campaign, or claim that the failed gate passed.

The third gate passes 264/265; terminal_pty_tools again snapshots a duplicated
FG_2 row. Source grounding finds its wait_for(BG_3) can return from the previous
screen without consuming the repaint queued by Ctrl-U; the preceding sleep does
not drain the PTY. Correct only that fixture's observation boundary: drain pending
PTY bytes to a quiet frame before applying the unchanged attribution oracle.
The failing old fixture is recorded in the gate; the corrected fixture passes on
the unchanged q4n executable (1.85 s). No TUI product/folding code changes. This is
a concrete gate-oracle correction in the existing recheck, not another review.

## 47k implementation and direct results

RED with q4n fixed but 47k unchanged: the native economical_defaults provider
prints `economical observations line 85` (the trailing error assertion), then
external_unknown; exit 1. It receives only the build head. The seam oracle also
links against the staged pre-47k tools object, not a simulated old behavior.

Compatibility choice: the default Lua workflow selects the 8192/2048 split;
explicit output_max_bytes retains head-first semantics unless output_head_bytes
is supplied. Custom Lua is full-read by default. Model callers can choose both
budgets explicitly. Text output has an explicit gap marker; binary head/tail are
separate values. Head/tail offsets and the bounded next_read identify exact bytes.
Files carry snapshot plus next_range; recovery adds canonical line ranges to the
existing retained process read tool.

The native provider now makes three requests. A 40058-byte exit-7 build shows the
trailing error while omitting NEEDED_MIDDLE=exact. The provider then calls
read_process_output on line 3 and receives exactly [20013,20033), including LF.
The same complete build output is recovered after reopening the session; the
command marker remains exactly X. Explicit file/exec limits, zero, ranges and
custom Lua full reads remain checked. Seven affected native tests pass (18.91 s).

47k single recheck: clipping/line boundaries, binary encoding, zero and explicit
budgets, metadata key order, raw command linkage, exact recovery after reopen,
and rolling-cache versus original identity. This found a new hot-path full-history
scan when sharing the recovery reader. Use an incremental locator-only index;
no output bodies are retained in it. Recheck/fixes remain in this layer; no third
review or unrelated assurance campaign. Full source gates are required by hook.

q4n delivered as 2b80669 through the locked hook: all 265 Debug tests pass
(375.63 s), owned C++/Lua lint and LuaLS pass. No suppressions were added.

Final 47k recheck findings/fixes: continuation hints themselves must avoid UTF-8
seams, not just the initial excerpt. The pre-fix hint oracle fails; hints now use
the same safe cut (tiny nonzero file hints allow at least four bytes to progress).
Override the command cache's next_offset/omitted_prefix_bytes with actual original
projection coordinates. Grow the locator index geometrically, preserving atomic
per-fact insertion without repeatedly copying its prefix.

The native build fixture is enlarged to 1200058 bytes, beyond the 1 MiB rolling
cache. On q4n-only code it fails the opening-build assertion (line 84), showing
why the rolling cache cannot stand in for the original. Final default projection
returns head [0,2048) and tail [1193914,1200058), with gap [2048,1193914).
The provider recovers line 3 exactly at [600013,600033). Delete the build source
before reopen and recover the entire original without a replay; the marker stays X.

Final affected Debug checks: 7/7 pass, 28.10 s. Release executable target builds
successfully (106 build actions). The required locked 47k source hook remains the
commit boundary, not a new assurance/review layer. Model/effort for this work:
gpt-6.1-sol, medium; deterministic test providers, no paid judge or retry panel.

47k first locked commit gate: interrupted with SIGTERM, not a test qualification
result. Git re-observation confirms no 47k commit. The release build and targeted
checks stay settled; one distinct required-gate retry uses the single hook lane.
