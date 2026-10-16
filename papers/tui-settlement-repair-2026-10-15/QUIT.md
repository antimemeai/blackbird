# Native terminal quit and command settlement

The final presenter gate failed `terminal_pty` at the first recovered-draft
submission's ordinary Ctrl-Q exit, `scripts/check-terminal-pty` line460 before
this work. The retained output still showed the `printf X` command as active.
The existing five-second exit assertion remains the direct oracle. The original
TemporaryDirectory removed its session and native raw recording on failure, so
the historical trace cannot establish where the command was waiting.

The first repair step preserves only failing fixture directories, reporting the
path on stderr; successful fixture directories still disappear. This changes no
product path, deadline, command, expected exit or screenshot. Run the existing
PTY driver once against the exact retained Debug candidate, without rebuilding,
and use any retained failure session to locate actual command ownership, child
exit/output closure, wait admission and Ctrl-Q processing.

`wait_for('[exit 0]')` searches the retained terminal screen, whose historical
status can precede the current submission. The command's effect file proves the
write occurred but does not prove publication completed. This is a diagnostic
caveat, not permission to weaken cancellation or replace the exit assertion.
Only after inspecting the actual failure should a unique post-call marker be
used where a scenario explicitly needs completed submission.

The source path under study is Composer quit → TerminalUI sticky turn wake →
CodingEngine exec wait → specific CommandJobs stop/join → terminal publication
and worker completion. Preserve the existing raw admission and prepaid cleanup
contracts. No extra worker, blanket cancellation shielding or timeout increase
is planned. AFL and its retained output remain untouched.

## First diagnostic run

The unchanged full driver passed all episodes in12.7819s against the retained
Debug executable SHA-256
`f0811c272105382feff06020e94acf1410d27ad8dc23304f6edbb3dba967939b`.
This executable is the exact candidate retained after the failed source gate;
no rebuild or source change preceded this run. The log remains in ignored
`context/tui-settlement-repair-2026-10-15/terminal-retained-candidate.log`.
Successful fixture directories were removed. This pass does not explain or fix
the historical failure, and no unchanged retry follows from it.

## Correct synchronization without weakening busy quit

The driver clears only the incoming byte buffer on `send`; the screen model keeps
its existing cells. `wait_for('[exit 0]')` can therefore return immediately from a
prior command's visible status. Neither a completed earlier command nor an effect
file just written proves that this invocation's native call returned. The normal
recovered-draft phases will append distinct, split-literal Lua print markers after
`arco.call('exec', ...)` and wait for those current markers. Keep all exactly-once
effect assertions and the five-second exit deadline.

Busy quit remains a separate direct obligation. Strengthen the existing
`sleep 30` phase so its actual shell prints `BUSY_QUIT_READY` using two printf
fragments before sleeping; the echoed Lua source does not contain that contiguous
token. Only then queue another draft, enter the unsent draft and issue Ctrl-Q,
retaining the same exit deadline and reopen/absence-of-extra-effect checks.
Thus the normal phases wait for their intended native completion while the busy
phase proves there was a live shell to stop. These oracle corrections establish
what each scenario exercises; they do not attribute the historical product hang
to stale screen matching or claim a native source repair.

The busy shell will also write its PID before READY. The fixture checks that
leader is alive at readiness and, after Ctrl-Q exits, that both leader and process
group disappear within the original five-second quit window. This directly checks
native child ownership in addition to the application exit and preserved drafts;
it adds no grace period beyond the existing quit deadline.

The first correlated-marker/actual-shell-readiness driver run passes against the
same retained Debug binary in46.3665s, preserving every existing individual
deadline and effect assertion. Its log is
`context/tui-settlement-repair-2026-10-15/terminal-correlated-quit.log`.
The PID/group ownership assertions were added after that run and still require
the coordinated built-source PTY check. Do not infer ownership qualification
for those new assertions from the earlier pass or attribute total elapsed time
without per-stage timing.

## Independent episode registration

The correlated full-driver run took46.3665s while its individual assertions all
passed. The monolithic CTest registration has a30-second process limit, so this
aggregate driver can lose all later diagnostics despite completing each direct
obligation within its original deadline. Split registration into independently
selectable controls, palette, capacity, editor, composer, state, comfort, avatar
and graphics episodes. Each native fixture and every state episode's cross-launch
sequence stay intact; default manual invocation still runs all episodes.

Hoist the shared wait/send/palette and output-draining exit helpers out of the
first/state episodes, and import shared JSON/signal helpers unconditionally.
Selected episodes must not depend on earlier episode execution. The root owner
will register each episode in CMake with the original30-second limit; this is
separation of independent scenarios, without increasing a native interaction
or command-settlement deadline. Per-episode CTest elapsed time then identifies
where aggregate time is spent.
