# Instrument panel brainstorm mockups (2026-10-18/19)

These are the self-contained HTML mockups from the overnight brainstorm of the
always-on 32" 4K portrait instrument panel, which renders in Ghostty through the
kitty graphics protocol. They were copied unchanged on 2026-10-19 from the
untracked brainstorm server directory
`.superpowers/brainstorm/8712-1792382538/content/`. Each file opens directly in
a browser; none loads external resources.

They are design exploration, not a selected design or an implementation.
The reference study is in
[../2026-10-19-agent-viz-references/README.md](../2026-10-19-agent-viz-references/README.md).

## Lineages

| Files | What they explore |
| --- | --- |
| `panel-v1`–`v2` | Whole-panel layout |
| `hero-stream`, `hero-cascade-v3`, `hero-fountain-v4`–`v8`, `hero-flows-v9`–`v10`, `hero-synth-v11`–`v12` | Early token-flow hero |
| `hero-sr71-v13`–`v15`, `hero-tokyo-v16`–`v17`, `hero-gotham-v18` | SR-71 hero and scenery |
| `hero-themes-v19`–`v23` | Theme set; v23 is the reference for themes |
| `ladder-v1` | Ladder view |
| `hero-flight-v24`, `hero-midi-v25`–`v29` | MIDI flight and fly-to-answer gates; v29 is the current hero |
| `agents-v1`–`v2`, `agents-ast-v3`–`v6` | Coding-agent view over the repository AST; v4 is the AST-corner basis named in `arconaut-jon`, v6 is the version the reference study diagnoses |
| `agents-city-v7`–`v11` | Code-city floor for agents, after the reference study |

Hero rules established in the brainstorm (from `arconaut-amr`): particle color
is token class, glow is source, scenery is data (grid speed is token rate,
afterburner is output plus reasoning rate), the jet stays above the particles,
and its attitude stays Mach-3 plausible (pitch at most about 1.7 degrees).

Open follow-ups: `arconaut-amr` (missiles and the Viggen encounters) and
`arconaut-jon` (a spatial basis for research and frumentarii agents).
