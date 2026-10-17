# Physical viewport observations

## Existing application RED

Before any native renderer edit, run the existing Debug application in an
isolated local session, open `/instrument` with Ctrl-], and resize101x30 to32x12.
No turn is submitted and no provider request occurs. The physical final column
is intended to be blank under the current31-column drawing policy, but actual
Microsoft terminal cells retain:

| Backend | Nonblank physical cells `(x,y,text)` |
| --- | --- |
| Ghostty | `(31,6,v)`, `(31,8,l)`, `(31,10,─)` |
| Alacritty | `(31,0,u)`, `(31,1,t)` |

Exact executable SHA256, full101x30/32x12 state, all384 physical cells, command
history and explicit terminal recordings are retained under ignored
`context/tui-terminal-edge-comparison-2026-10-16/red/recorded/`. Both isolated
sessions and their own daemons were closed. The initial Ghostty probe retained
its full failing plane but could not fetch an automatic recording because that
feature was disabled. The recorded repeat explicitly starts recording; this is
a harness capture correction, not an intermittent product result.

The unchanged Debug application's SHA256 is
`8a0c78a3a41deacefab776583a34b7ac79b43ab387b20ef2713c1717376e5f03`.

An API study also found that repeated alternate-screen entry (`CSI ?1049h`)
clears Ghostty's alternate screen but preserves Alacritty's existing contents.
Therefore a startup pre-seeding wrapper is unsuitable as the common RED oracle.
The actual application resize manifestation above does not depend on it.

## Candidate terminal qualification

`scripts/check-terminal-edge` reads the direct scenes in PLAN.md and requests
all physical cells through the adopted Microsoft driver. Each completed episode
checks37 planes: default/styled blank tails, sentinel rows and bottom-right,
wide/combining text and overwrite, 101x30→32x12→101x30, 2x2, 1x1 and return to32x12.
Every plane also checks cursor0,0, visible cursor, wrap-on and owned terminal
modes. Clean exit requires exit0, no signal and released alternate/paste modes.
All literal CLI responses are retained once, with phase/history indexes; automatic
recording captures the startup frame as well as the remaining episode.

The initial Ghostty/addressed episode passes all37 cell/style/mode planes and
exit0, then the harness incorrectly reports cleanup failure when daemon-stop
returns documented exit3 because close already stopped that session's daemon.
The correction accepts stop0 or absent-session3; subsequent complete native
episodes exercise the corrected cleanup. No physical assertion changes.

### Corner counterexample

Ghostty's `corner` policy fails at Unicode scene u after recovery to2x2, sequence20.
The bottom-right cell should contain e+combining-acute, with an ordinary blank
immediately to its left. Actual full physical cells instead contain:

| Cell | Expected | Actual |
| --- | --- | --- |
| `(0,1)` | space | space+combining-acute |
| `(1,1)` | e+combining-acute | e |

The packet brackets the whole scalar and its combining tail with DECAWM
disable/enable, so this is not a bracket inserted between the scalar and tail.
It is a concrete incompatibility of this candidate with the selected Ghostty
profile. Alacritty completes the same37-plane corner episode successfully.
Do not describe the candidate as portable across these profiles or rank it as
a qualified winner without a separately justified change and its owning recheck.

The exact failing attempt is retained under
`context/tui-terminal-edge-comparison-2026-10-16/oracle/ghostty-corner-ce77d49665/`;
its literal response index records the expected/actual cell discrepancy and its
native events retain READY20 at2x2. The attempt is preserved unchanged.

### Completed qualification scope

| Policy | Ghostty | Alacritty |
| --- | --- | --- |
| addressed | 37 original planes pass;5 focused planes and corrected cleanup pass | 37+5 planes pass |
| erase | 37+5 planes pass | 37+5 planes pass |
| full | 37+5 planes pass | 37+5 planes pass |
| corner | Original sequence20 fails combining at bottom-right;5 focused wide-corner planes pass | 37+5 planes pass |

The five additional planes are the focused2x4 episode: sentinel s, Unicode u,
overwrite o, styled blanks c and default blanks b. This places the specified row3
width-two glyph exactly at bottom-right, a case the original geometry list did
not reach. All four policies pass it on both profiles; it does not excuse the
distinct Ghostty combining failure. The default full episode now includes2x4
geometry and has43 planes for future required source checks. The completed
comparison retains its original37-plane attempts plus the focused5-plane checks
instead of relabelling them as a43-plane run.

The deliberate legacy cropped fixture fails the full physical plane at startup:
its lowercase row-end sentinel appears at x99 and physical x100 is blank, whereas
the101-column specification requires the lowercase sentinel at x100. This
negative control confirms the direct cell observation includes the final column;
its READY metadata deliberately describes the cropped committed grid.

[Run index](ORACLE_RUNS.tsv) records all17 candidate/negative attempts:319 passing
physical planes, the original cleanup API failure and both physical failures
(disqualified corner and intended legacy negative). It records the exact fixture
SHA256 and ignored original paths for every attempt. Each attempt retains its
own literal state/cell responses, native events, command history and terminal
recording. This qualification does not claim GPU paint timing or actual-main
gutter repair; the application width rule remains unchanged during comparison.

The comparison keeps corner as a disqualified prototype and measures
only addressed, erase and full. No performance winner is claimed by this oracle.
