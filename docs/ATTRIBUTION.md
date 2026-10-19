# Commit attribution

Operator decision, 2026-10-19. Every commit that an agent wrote names that agent in
GitHub's `Co-Authored-By` trailer and records where the work came from in
machine-readable trailers. The operator's Git identity stays the author and the
committer. Trailers go in one block at the end of the message, separated from the
body by a blank line.

## Who adds what

| Writer | Trailers |
| --- | --- |
| Claude Code session | `Co-Authored-By: Claude <model> <noreply@anthropic.com>` (as the harness provides) and `Claude-Session: <session id>` |
| Blackbird autonomous run | `Co-Authored-By: Blackbird (<model>) <blackbird@noreply.antimeme.ai>`, `Blackbird-Run: <campaign>/<name>`, `Blackbird-Model: <provider> <model> <effort>`, `Blackbird-Generation: <installed generation>` |
| Blackbird session with the operator in the seat | the same `Co-Authored-By`, `Blackbird-Model`, and `Blackbird-Session: <session directory>` instead of `Blackbird-Run` |
| Blackbird participant or colleague whose output is integrated | `Co-Authored-By: <Colleague> (<model>) <blackbird@noreply.antimeme.ai>`; the integrating writer adds its own trailers too |
| A review whose findings this commit integrates (Kimi, a Claude or ChatGPT colleague) | `Reviewed-By: <Reviewer> (<model>)` and `Review: <path under papers/>` |

- `Blackbird-Run` is relative to `~/projects/blackbird_runs/`. That directory holds
  the run's `meta.txt`, task, session originals and collected views.
- `Blackbird-Generation` names the installed generation that ran the agent, for
  example `generation-0BmneCkH`. Use the source commit when the run used a build
  tree instead.
- Jev decisions are advisory and get no trailer. A human-only commit gets none.

## Example

```text
Complete the slash palette's singular and plural commands

<body>

Co-Authored-By: Blackbird (gpt-6.1-sol) <blackbird@noreply.antimeme.ai>
Blackbird-Run: 2026-10-19-sol/slash
Blackbird-Model: openai gpt-6.1-sol medium
Blackbird-Generation: generation-0BmneCkH
```

## Reading it back

```sh
git log --format='%h %s%n%(trailers:key=Blackbird-Run,valueonly)'
git log --format='%h %(trailers:key=Co-Authored-By,valueonly,separator=%x2C )'
git log --grep='^Blackbird-Model: openai gpt-6.1-sol' --format='%h %s'
```

Commits made before this convention carry whatever attribution they had. Their
messages are not rewritten.
