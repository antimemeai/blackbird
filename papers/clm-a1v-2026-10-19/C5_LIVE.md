# C5 live check: does the model follow the context budget? (2026-10-19)

**Run.** `~/projects/blackbird_runs/2026-10-19-clm/c5-luna`, using the campaign runner (sandboxed) with a new
`PRELUDE` launch:

```
name=c5-luna
provider=openai
model=gpt-6-luna
effort=high
checkout_commit=1f289453df440db4a8601b357f33692917ff8dc9
blackbird_sha256=a5b7da5204a06a4a0d790a177228bf351759c47bce9db7a3a42ade23be2d2d12
start=2026-10-19T12:41:41Z
prelude=/lua assert(arco.call('context_budget',{proposal={enabled=true,trigger_bytes=61440,target_bytes=30720,reason='a1v C5 live check: small trigger so the nominal path fires in a short session'}}).staged)
end=2026-10-19T12:43:26Z
```

**Setup.**
- The prelude staged a 60 KiB trigger and a 30 KiB target, which activated before the task.
- The task was a read-heavy trace of raw instrumentation through `blackbird-src/` (a read-only copy of this
  worktree), writing `FLOW.md`.
- Up to 2 continuations were allowed; one was used.

**Evidence.** The exact wire bodies were read from raw site 1301 with `request-sizes/c5scan.py`.

| Launch | Requests | CONTEXT_SIZE line | Trigger guidance | Input bytes (first → last) | Context tool calls |
|---|---|---|---|---|---|
| task (turn 0) | 12 | every request | requests 4–12 | 1,024 → 362,248 | none |
| continue (turn 1) | 7 | every request | every request | 366,307 → 787,382 | none |

**Result.** The machinery delivered exactly what was designed: the size on every request, and the full
CONTEXT_BUDGET guidance (`src/coding.cpp`) on 16 consecutive requests. `gpt-6-luna` at high effort ignored
it. It never called `context_view`, `context_manage` or `context_budget`, and finished at about 26× the
target. No provider refused the request at 787 KB of input (about 933 KB on the wire). This matches the 2026-10-17
campaign, where none of 39 runs used a context tool.

**Conclusion.** Advisory guidance alone does not make this model manage its context. The nominal path needs a
stronger mechanism before C4's backstop matters. The options and the operator decision are in PLAN.md.
