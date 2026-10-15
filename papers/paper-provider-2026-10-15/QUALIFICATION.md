# Paper Instruments qualification

Worktree: `/Users/patrickbeam/projects/blackbird-paper-provider`.
Branch: `work/paper-provider-2026-10-15`, based on `d987f6f`.
No merge into master or executable activation in another checkout.

## Direct outcomes

- Meaningful red: the original release executable refuses registration of
  `responses-portable` with `invalid_range`.
- Initial Mac Debug focused checks: export/transport, colleague and session tests
  pass. The native fixture passed after correcting two fixture setup mistakes.
- Final Linux Debug on Neuroses: all four affected cases pass: responses_provider,
  registered_responses_native, provider_colleague and session_store. This includes
  portable strict:false, rejected interactive effort changes and retained settings.
  Logs: ignored context/linux/run-kcayly19/checks.log.
- Live optimized candidate: a capped default-derived workflow calls read_file,
  receives a fresh random file nonce, submits the tool result and returns that
  nonce. Both Responses calls complete. Paper's reported successful-run usage is
  12,474 input and 91 output tokens across those two calls (44 reasoning tokens).
  These are provider-reported counters, not an invoice or price estimate.
- The unmodified default workflow repeats the actual read_file test successfully.
  An ordinary reopen with no provider/model/effort flags then answers a follow-up
  from the retained conversation without another tool. All three requests complete.
- Staged source/document files and successful live session captures were scanned
  for the actual key without displaying it. The key is stored only in the owned
  private provider auth store; it is not copied into the worktree.

Exact live logs and retained sessions remain under ignored context/. The first
live experiment's unsupported top-level max_attempts was corrected in its test
workflow. A subsequent experiment exposed Paper's automatic strict-schema
normalization; the portable adapter now preserves optional arguments with an
explicit strict:false, and the successful live checks use that correction.

## Local build follow-through

The installed Clang 23.1.2 child-process mode stalled on this host. Controlled
retries retained the actual logs. The same installed compiler's documented
-fintegrated-cc1 flag is selected in this worktree's ignored Debug/Release caches;
tracked presets and the original checkout remain unchanged. Compiler identity
checks and the normal source gate use the actual cache. Raw build IDs include
this flag. The optimized executable with this setting passed the live tests.
Recreate the local cache choices with:

```sh
cmake --preset debug -DCMAKE_CXX_FLAGS=-fintegrated-cc1
cmake --preset release -DCMAKE_CXX_FLAGS=-fintegrated-cc1
cmake --build --preset release --target blackbird
```

## Bound and remaining limitation

The independent design/code reviews and the one recheck's corrections are in
[REVIEW.md](REVIEW.md). No new dependency or runtime is adopted. The shared
successor-seed validator requires encrypted reasoning, so seeds containing Paper
plaintext reasoning are refused. This delivery leaves that context contract
unchanged and documents it in the operator guide; ordinary full-history tool
loops, context editing, session reopen and fresh sessions remain available.

## Complete local source gate

The first full Mac Debug run completed145/148 tests in330.94seconds. command_jobs
missed its fixture startup handshake, coding hit its60-second watchdog, and
Microsoft tui-test was not installed in this fresh worktree. The explicitly
adopted pinned tool was installed and its version checked. The three failed
cases then passed sequentially without source/assertion/deadline changes:
3.01s,4.80s and14.34s. The normal source pre-commit hook is rerun with the
prerequisite present; its actual output remains in context/paper-commit.log.

The recheck completed147/148 in259.33seconds. command_jobs_pty failed at
scripts/check-command-jobs-pty:566: `quit did not complete job cleanup`.
The other147 cases, including all provider cases, passed. The normal source hook
therefore refused that commit attempt. No hook override, source commit, push or
merge was performed at that checkpoint. The implementation remained staged. Repair
of the existing command-job quit path is outside this provider unit's declared
scope; the blocker is recorded on arconaut-7fp. No third full-suite campaign is
was started for the provider unit. Separate source lint followed because the hook
stopped before reaching it.

`scripts/rigor lint` completed successfully: owned C++ formatting, native source
and header analysis, Lua formatting/syntax and LuaLS all pass. It introduced no
suppression annotation. At that checkpoint the normal source gate remained failed
on the command-job PTY quit case; full lint success did not override that failure.

## Fresh-start follow-up and publication

The operator then rejected implicit global-session resume after a launch without
`--session` selected the old Arconaut history. The independently scoped
[fresh-start repair](FRESH_START.md) removes that behavior in both launch paths.
The rebuilt optimized executable passes the operator's actual Paper launch
options with `--once /session`; no provider request was needed for that startup
check. Release and Debug each pass the four affected session/restart cases.

The normal source commit hook for the combined implementation now passes all
149 Debug tests in316.27seconds, including command_jobs_pty in32.05seconds, plus
full C++ formatting/native/header analysis and Lua formatting/syntax/LuaLS.
Source commit: `245d8e9`. Log: context/fresh-start-commit.log. No source-hook
override or source modification to the command-job shutdown path was used. The
earlier timeout did not recur; this run does not establish its root cause.
This successful normal gate supersedes the earlier publication blocker.
