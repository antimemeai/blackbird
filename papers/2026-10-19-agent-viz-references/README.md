# Agent-swarm visualization references (frumentarius report)

Date: 2026-10-19. Scope: reference visualizations for the always-on 32" 4K portrait
instrument panel that shows many concurrent coding subagents moving through a
codebase. The panel renders in Ghostty through the kitty graphics protocol.

Every URL below came up in a search result or a fetched page during this
scouting pass. Where a page loaded but did not confirm a detail, the entry says
so. Some entries carry "(unverified)" notes; those details come from memory or
secondhand descriptions and should be checked before anyone relies on them.

## Diagnosis of the current mockup (agents-ast-v6)

The current mockup puts a tidy tree (dirs → files → functions) on a perspective
floor. Agents fly between nodes on parabolic arcs, and a callout list sits in a
separate column. Measured against the references below, it has five specific
gaps. I think these, more than taste, explain why it reads as uncanny:

1. **The height channel carries nothing.** Arc height depends only on hop
   distance. The eye reads the 3D motion as meaningful and then finds no meaning
   in it. That is the "almost cool" part.
2. **The floor has no mass.** Every node is a dot of the same weight, so a
   200-line file and a 9,000-line file look the same. Gource, the code cities
   and the software maps all give structure weight (area or size from LOC).
3. **The floor has no memory.** A node an agent edited ten minutes ago looks
   exactly like one nobody has touched. Every strong reference accumulates
   something: heat, glow, touch state or trails.
4. **Labels sit away from the actors.** Reading the panel means bouncing
   between the callout list and the glyphs. ATC data blocks, Gource and
   code_swarm keep the label riding on the actor.
5. **There is perspective but no depth cue.** Nothing ties a glyph in the air
   to its spot on the floor: no stem, no shadow, no drop line. Elite, EVE and
   Homeworld solved this decades ago.

## Top 8, ranked by relevance

| # | Reference | Technique to steal |
|---|---|---|
| 1 | **Mindwalk** (cosmtrek, 2026) | Persistent per-file *touch state* (unvisited dark → seen → read → edited), cool versus warm for observing versus mutating, and a scrubbable activity histogram with event glyphs |
| 2 | **Software maps / CodeCharta / CodeCity** (Bohnet & Döllner 2011; MaibornWolff; Wettel & Lanza 2007–08) | A *stable 2.5D treemap floor*: footprint = LOC, height = a metric, color = another metric. Structure gets mass and the layout never jitters |
| 3 | **Gource** (Andrew Caudwell, 2009) | Actor *labels ride with the actors*, beams run from actor to file at the moment of action, touched files flash and then settle, and idle actors fade out |
| 4 | **ATC radar data blocks with history dots** (FAA STARS/ERAM; generic PPI track display) | A compact *data block on a leader line* for each actor, a short trail of *history dots*, and a velocity or intent vector. This replaces the separate callout list |
| 5 | **Elite 3D scanner stalks / EVE tactical overlay / Homeworld sensors manager** | *Drop lines (stems)* from each airborne glyph to its floor position, plus range rings on the ground plane. Height becomes readable |
| 6 | **Marey / Ibry string chart** (Ibry, published by Marey 1885; Bostock's D3 and Observable versions) | A *time × linearized-structure* strip. Each agent is a polyline, slope shows how fast it traverses code, and convergence or collision shows up as crossing lines. The full history persists |
| 7 | **Hägerstrand space-time cube** (time geography, 1960s–70; Kraak's digital revival) | *Spend the z axis on time.* An agent that dwells on a node draws a vertical "station", and hops become slanted segments. Height becomes honest |
| 8 | **Hierarchical edge bundling** (Danny Holten, InfoVis 2006) | Route agent hops *along the tree path* (B-splines bent toward common ancestors) instead of free arcs. Motion then carries structural meaning, and many hops bundle into readable streams |

Runners-up that nearly made the top 8: **code_swarm** (actor-centric gravity,
files flying to the people who touch them), **Wind Map** (decaying particle
trails as the history medium), the **Perfetto/trace waterfall** (one track per
agent, flow arrows for spawns) and **storyline charts** (xkcd 657; Tanahashi &
Ma 2012).

### Why these eight, in more detail

1. **Mindwalk** is the closest prior art I found: coding-agent sessions from
   Claude Code and Codex replayed on a 3D map of a repository, with both a
   radial-tree view and a treemap "terrain" view. Its contribution is the
   *memory model*. Each file keeps its deepest touch, and deleted files persist
   as wireframe ghosts. The timeline is a histogram colored by observation
   versus mutation, with marks for compaction (◇), subagent launch (○) and user
   turn (›). Weaknesses: it is a single-session replay tool, not a live
   multi-agent instrument. From the README I can't tell whether it scales past
   one agent's colors.
2. **Software maps (2.5D treemaps)** fix the massless floor. They have a
   twenty-year academic lineage (HPI Potsdam, which spun out the company
   Seerene) and an active open-source tool, CodeCharta. Stable layout matters
   most for an always-on panel, because your eye learns where `src/` is.
   Weakness: tall buildings occlude, so cap the height or use a
   near-orthographic camera.
3. **Gource** is the canonical "actors move through code structure over time"
   piece. Its legibility comes from the label staying on the actor and from the
   beam, a transient line from actor to file that marks the action itself.
   Weaknesses: the force layout breathes and jitters, history is lost because
   nothing accumulates beyond brief flashes, and it is tuned for cinematic
   replay rather than monitoring.
4. **ATC data blocks** are the professional answer to labeling many moving
   actors on a dense display that people watch for hours. Each block holds a
   few fields (call sign, altitude, speed) on a leader line whose direction the
   controller can rotate to avoid overlaps. A few history dots show recent
   motion without a full trail. A "quick look" mode can expand blocks for other
   sectors. The direct mapping is agent id / current op / node / tokens in a
   two-line block.
5. **Elite, EVE and Homeworld stems** solve "where is that glyph relative to
   the floor?" Elite (1984) drew each contact as a dot with a stick down or up
   to the scanner ellipse. EVE draws arcs projecting off-plane distance onto its
   tactical plane, with range rings. Weakness: Elite: Dangerous players
   complained about log scales and asked for arcs instead of lines, which tells
   you to keep the distance encoding linear and the reference plane obvious.
6. **The Marey chart** is the classic many-actors-over-time graphic and the
   cover of Tufte's *Visual Display*. Linearize the code tree into a DFS order
   along one axis and put time on the other. Each agent becomes a line, two
   agents in the same file become an intersection, and a stalled agent becomes
   a flat run. It is a strong companion strip to the spatial view on a portrait
   panel, which has room for both.
7. **The space-time cube** is the principled version of "things in the air
   above a floor". If z means time, height is data and not decoration. Use a
   short rolling window, such as the last N minutes rising above the floor and
   fading. Weakness: 3D occlusion and perspective distortion. The immersive-STC
   literature (arXiv 1908.00580) evaluates this.
8. **Edge bundling** gives hops a meaning: a hop from `src/a.cpp` to
   `tests/a_test.cpp` should visibly travel up to `/` and back down. Bundling
   turns many agents' hops into streams that show which subtrees exchange
   attention. Weakness: bundles hide individual paths, so highlight the most
   recent hop per agent.

## Full catalog

Format for each entry: **Name**, creator and year, URLs, *Encodes* (data →
channel), *Works because*, *Weak*.

### A. Software evolution and repository activity

**A1. Gource**, Andrew Caudwell, 2009–present (GPL, C++/OpenGL)
- Project: https://gource.io/ · Screenshot: https://gource.io/images/gource-linux.jpg · Code: https://github.com/acaudwell/Gource · Videos index: https://github.com/acaudwell/Gource/wiki/Videos (for example, Git's history at https://www.youtube.com/watch?v=GTMC3g2Xy8c, and a collage at https://www.youtube.com/watch?v=E5xPMW5fg48)
- *Encodes:* directory tree → radial force layout (root at center, dirs as branches, files as leaves). Committer → floating avatar with a name label. Commit action → beam from user to file. File type → color.
- *Works because:* labels ride with the actors, action is shown as a transient beam, touched files flash, and idle users fade away, so the scene stays sparse.
- *Weak:* the force layout keeps moving, so there is no stable mental map. There is little persistent history. Speed and camera choices are cinematic, not quantitative.

**A2. code_swarm**, Michael Ogawa (UC Davis, Kwan-Liu Ma's lab), 2008 (Processing, GPLv3)
- Old project page: http://vis.cs.ucdavis.edu/~ogawa/codeswarm/ (cited in a 2008 mailing-list post; may be dead) · Python video: https://vimeo.com/1093745 · Coverage: https://flowingdata.com/2008/06/19/an-experiment-in-organic-software-visualization/ · LWN: https://lwn.net/Articles/381367/
- *Encodes:* developer → name label that drifts. File commit → colored dot (color = file type) that flies to the developer. Developer label brightness → recent activity.
- *Works because:* it is actor-centric. Files gravitate to the people who touch them, so clusters of collaboration form on their own. Its organic motion has physical inertia, and decay or fade carries recency. (Unverified: the published videos include an activity histogram strip at the bottom, as I recall; check the Vimeo video.)
- *Weak:* there is no code structure. Position comes from physics, not the tree, so you can't tell *where* in the code anything happens.

**A3. Logstalgia (ApachePong)**, Andrew Caudwell
- https://logstalgia.io/ · Videos: https://github.com/acaudwell/Logstalgia/wiki/Videos
- *Encodes:* request → ball colored by host. Requested path → target row on the right, with paths grouped by common prefix under headings. Success or failure → paddle hits or misses.
- *Works because:* every event has a source, a flight time and a destination. Outcome is shown by what happens at arrival (a hit, or a miss that sails through), and prefix grouping squeezes a deep namespace into the available rows. `--paddle-mode pid` gives one paddle per process, which is the multi-actor version.
- *Weak:* it is pure spectacle at volume, and it has no history once balls leave.

**A4. CodeCity**, Richard Wettel & Michele Lanza (USI Lugano), 2007–2011
- https://wettel.github.io/codecity.html · Screenshot (JDK 1.5): https://wettel.github.io/pics/codecity_screenshot.png · ICSE 2011 controlled experiment: https://www.inf.unibz.it/~rrobbes/p/ICSE2011-codecity.pdf · VISSOFT 2007: https://www.si.usi.ch/assets/publications/conf/vissoft/vissoft2007/WettelL07.pdf
- *Encodes:* package → district, class → building, and metrics → building height, base and color (polymetric views).
- *Works because:* structure gets *mass*, and outliers jump out as skyscrapers. A controlled experiment found it a viable alternative to non-visual techniques.
- *Weak:* it is static. Agents and time are absent from the core tool, though the evolution extensions exist. 3D occlusion is a problem.

**A5. CodeCharta**, MaibornWolff (BSD-3, active)
- https://github.com/maibornwolff/codecharta · Article: https://www.maibornwolff.de/en/know-how/code-visualisation-with-codecharta/ · heise.de case study (Deutsche Bahn): https://www.heise.de/en/background/CodeCharta-Visualize-software-quality-clearly-as-a-cityscape-10322754.html?view=print
- *Encodes:* file → building. Area, height and color → selectable metrics (for example LOC, complexity, coverage). It can import git metrics.
- *Works because:* the treemap layout is stable, so the same city can be compared across time. The heise case study shows a city turning from red to green as coverage improved.
- *Weak:* it is a snapshot tool with no live actors, and it is browser-based.

**A6. Software maps (2.5D treemaps)**, Bohnet & Döllner 2011, HPI Potsdam (Limberger, Scheibel, Trapp); commercialized as Seerene
- "Visual variables and configuration of software maps": https://paperity.org/p/296608399/visual-variables-and-configuration-of-software-maps · Limberger dissertation: https://publishup.uni-potsdam.de/files/63201/limberger_diss.pdf · LOD and clutter reduction: https://www.scitepress.org/Papers/2017/62675/pdf/index.html · EvoCells (treemap layout for evolving trees): https://www.scitepress.org/publishedPapers/2018/66171/pdf/index.html · Seerene: https://tech.eu/brief/seerene-14-million
- *Encodes:* containment treemap embedded in 3D. Footprint, height and color → metrics, plus "developer activity".
- *Works because:* it is a careful study of *which visual variable carries which metric*, with adaptive level-of-detail aggregation for big trees and layouts designed to stay stable as the tree evolves (EvoCells).
- *Weak:* it is aimed at managers in boardrooms, and it does not show live actors.

**A7. Software Cartography / CodeMap**, Adrian Kuhn, David Erni, Loretan, Oscar Nierstrasz (Bern), 2008–2011
- WCRE 2008 paper: https://scg.unibe.ch/assets/archive/papers/Kuhn08bSoftwareMap.pdf · Kuhn PhD: https://scg.unibe.ch/assets/archive/phd/kuhn-phd.pdf · Erni MSc (CodeMap): https://scg.unibe.ch/assets/archive/masters/Erni10a.pdf
- *Encodes:* file vocabulary (LSI → MDS) → 2D position. Size → hills on a *terrain* with contour lines. Thematic overlays go on top.
- *Works because:* it has a *consistent layout* across versions and themes, so one map gets reused for many overlays. A user study found it most useful for showing search results and call hierarchies *on the map*.
- *Weak:* semantic layout isn't the directory tree, so it may surprise people who think in paths.

**A8. Evolution Matrix**, Michele Lanza, IWPSE 2001
- Paper: https://www.inf.usi.ch/lanza/Downloads/Lanz2001c.pdf · Follow-up (Lanza & Ducasse 2002): https://www.inf.usi.ch/lanza/Downloads/Lanz2002a.pdf
- *Encodes:* column → version, row → class. Box width and height → metrics such as method and attribute counts.
- *Works because:* small multiples over time give a *vocabulary of patterns* (pulsar, supernova, white dwarf, idle). Shapes become words.
- *Weak:* class-level only, and it has no actors.

**A9. SeeSoft**, Stephen Eick, Joseph Steffen, Eric Sumner (Bell Labs), IEEE TSE 1992
- Survey listing: https://www.cs.kent.edu/~jmaletic/softvis/SoftVisTools.html · Lecture slides: https://people.cs.gmu.edu/~tlatoza/teaching/swe795f19/Lecture%207%20-%20Software%20Visualization.pdf
- *Encodes:* each source line → a one-pixel-high row in a newspaper-column strip per file. Color → line statistic (age, author, change count, hot spot).
- *Works because:* it fits about 50k LOC on one screen. It is the original *pixel-per-line* density trick and maps directly to kitty-graphics pixels: each agent's edits could light up the exact lines.
- *Weak:* it shows no motion or actors, and the color coding needs a legend.

**A10. Hierarchical Edge Bundles**, Danny Holten (TU Eindhoven), IEEE TVCG 12(5), 2006
- Publication: https://research.tue.nl/nl/publications/hierarchical-edge-bundles-visualization-of-adjacency-relations-in/ · PDF copy: https://www.cg.tuwien.ac.at/courses/InfoVis/HallOfFame/2007/Grasberger/InfoVis2006%20Hierarchical%20Edge%20Bundles%20-%20Visualization%20of%20Adjacency%20Relations%20in%20Hierarchical%20Data%20%5BDanny%20Holten%5D.pdf
- *Encodes:* adjacency (calls, imports) between tree leaves → B-splines whose control points are the tree path, so edges bundle through common ancestors.
- *Works because:* it shows *flow along structure*, and a bundling-strength parameter trades individual paths against aggregate streams.
- *Weak:* individual edges get lost inside bundles.

**A11. Code Flows**, Alexandru Telea & David Auber, EuroVis 2008
- PDF: https://webspace.science.uu.nl/~telea001/uploads/PAPERS/EuroVis08/paper.pdf · Record: https://research.rug.nl/en/publications/code-flows-visualizing-structural-evolution-of-source-code/
- *Encodes:* AST of one file across versions → icicle columns joined by flow tubes. Splits, merges, insertions and deletions → tube events.
- *Works because:* it treats code *structure* as a flowing medium over time and makes edit events visible as topology changes.
- *Weak:* one file at a time, and it is hard to read without training.

**A12. History Flow**, Fernanda Viégas, Martin Wattenberg, Kushal Dave (IBM), CHI 2004
- https://bewitched.com/historyflow.html · MoMA: https://www.moma.org/collection/works/110349
- *Encodes:* versions → columns, text spans → bands colored by author, and band continuity → text that survives.
- *Works because:* conflict shows up as visible *shapes*. Zigzags mean edit wars, and black gashes mean vandalism. The pattern means something before you read any label.
- *Weak:* one document at a time.

**A13. GitHub Next repo-visualizer**, Amelia Wattenberger, 2021
- Coverage: https://flowingdata.com/2021/08/10/visualizing-github-repos/ · Action: https://awesome.ecosyste.ms/projects/github.com%2Fgithubocto%2Frepo-visualizer
- *Encodes:* file → circle (size = bytes, color = type). Folder → enclosing circle, laid out by recursive force packing.
- *Works because:* it gives one glance at the repository's shape with mass. A circle-packed floor is an alternative to a treemap.
- *Weak:* static, and circle packing wastes area compared with treemaps.

**A14. Sourcetrail**, Coati Software, open-sourced 2019, archived 2021
- https://github.com/coatisoftware/sourcetrail
- *Encodes:* the symbol graph (classes, functions, call and usage edges), with code shown next to the graph.
- *Works because:* graph and code are linked bidirectionally, and the graph is always focused on one symbol and its immediate neighborhood.
- *Weak:* it is a focus view, not a whole-repo overview, and it is unmaintained.

### B. Multi-agent, swarm and distributed-system activity

**B1. Mindwalk**, cosmtrek, 2026 (Go, local)
- https://github.com/cosmtrek/mindwalk · Terrain screenshot: https://github.com/cosmtrek/mindwalk/raw/master/assets/screenshot-terrain.png · Agents-lens screenshot: https://github.com/cosmtrek/mindwalk/raw/master/assets/screenshot-agents.png · Show HN mirror: https://zeli.app/en/story/48878682
- *Encodes:* repo → radial tree or treemap "terrain" in a dark "night map". Each file's deepest touch → color (seen = moss green, read = moonlight blue, edited = warm amber, unvisited = dark). Deleted files → wireframe ghosts. Timeline → bucketed histogram (cool = observe, warm = mutate) with glyph marks for compaction, subagent launch and user turn. The HUD shows a "friction" strip: error rate, churned files, and edits since the last verify.
- *Works because:* everything is dark until touched, so the map *is* the session's history. Its warm/cool semantic split is cheap to read.
- *Weak:* it is a single-session replay, not live, and multi-agent support isn't documented.

**B2. codemap**, JamsusMaximus, 2026 (TypeScript)
- https://github.com/JamsusMaximus/codemap · Write-up: https://blog.brightcoding.dev/2026/10/11/jamsusmaximuscodemap-real-time-pixel-art-agent-visualization
- *Encodes:* folder → room (placement ranked by git activity), file → desk, agent → pixel character. Desk screen color → read (yellow) or write (green). Speech bubble → current tool and file. Bouncing → blocked or waiting for permission.
- *Works because:* it has an explicit, glanceable *blocked* state, which is the thing an operator most needs.
- *Weak:* rooms reorganize on every commit (layout churn). It gets cluttered past about 10 agents, and it is decorative pixel art rather than dense.

**B3. Pixel Agents**, Pablo De Lucca, 2025–26 (VS Code extension)
- https://github.com/pablodelucca/pixel-agents · Screenshot: https://github.com/pixel-agents-hq/pixel-agents/raw/main/webview-ui/public/office.png
- *Encodes:* terminal → character. Activity → animation (typing for edits, reading for search). Speech bubble → waiting or permission. Subagents → ephemeral characters linked to their parent.
- *Works because:* the parent-child link is visible, and it carries attention signals (bubbles and chimes).
- *Weak:* it is mostly charm and carries little information density. Position in the office has no code meaning.

**B4. jcode live swarm page**, 1jehuang, 2026
- Release note: https://newreleases.io/project/github/1jehuang/jcode/release/v0.47.0
- *Encodes:* agent ownership tree with animated status and details for the selected agent.
- *Works because:* it is a TUI-native, tree-of-agents view and the nearest peer to Blackbird's medium.
- *Weak:* it has no code-space map, and I saw only the release note.

**B5. Claude Code hooks multi-agent observability**, disler (IndyDevDan), 2025–26
- Docs: https://docsearch.algolia.com/mcp/docs/repo/disler/claude-code-hooks-multi-agent-observability
- *Encodes:* hook events → live event stream, "pulse" chart, and filters per session or agent.
- *Works because:* the event pipeline is simple (hooks → server → WebSocket) and each agent has its own pulse.
- *Weak:* it is a dashboard of lists and charts with no spatial code view. Swim lanes appear only in derivatives, which I did not verify.

**B6. Generative Agents / Smallville**, Joon Sung Park et al. (Stanford/Google), UIST 2023
- Repo docs: https://docsearch.algolia.com/mcp/docs/repo/joonspk-research/generative_agents · Coverage: https://decrypt.co/136798/chatgpt-ai-the-sims-smallville
- *Encodes:* 25 agents → sprites on a tile map, with the current action shown as text or emoji next to each agent and a replay URL by time step. (The emoji detail is unverified in this pass.)
- *Works because:* the action label rides with the sprite.
- *Weak:* there is no aggregate or history layer, and replay is a debugging tool.

**B7. METR's OpenAI / Hugging Face incident investigation**, METR, 2026-08
- https://metr.org/blog/2026-08-26-openai-hugging-face-incident-investigation/
- *Encodes:* hundreds to thousands of agents → hourly message-category rate charts, discovery-over-time curves, share-of-agents-joining curves, and an "anatomy of an agent" path diagram.
- *Works because:* it shows how analysts actually *read* a large agent swarm after the fact: rates, adoption curves and categorical clustering, not sprites.
- *Weak:* it is a static forensic report, not an instrument.

**B8. Netflix Vizceral**, Netflix, 2016 (Apache-2.0, unmaintained)
- https://github.com/Netflix/vizceral · Example image: https://github.com/Netflix/vizceral/raw/master/vizceral-example.png
- *Encodes:* service graph with three zoom levels (global, region, service). Traffic volume → animated dots streaming along edges (the error-color details are in the wiki, which I did not read).
- *Works because:* motion is *along edges* and its rate is proportional to volume. Semantic zoom between levels.
- *Weak:* the particle density is hard to read quantitatively, and the project is no longer used internally.

**B9. Kubernetes Operational View (kube-ops-view)**, Henning Jacobs
- https://codeberg.org/hjacobs/kube-ops-view · Screenshot: https://codeberg.org/hjacobs/kube-ops-view/media/branch/main/screenshot.png
- *Encodes:* node → box with one CPU cell per core, filled to requests or usage, plus a memory bar. Pod → small box with border color for state (green running, yellow pending, red error) and mini CPU and memory bars. Pod creation and termination are animated.
- *Works because:* it is extremely dense small multiples, every element is data, and it has a retro terminal look that suits a TUI. It is a direct model for "many agents as tiles with micro-bars".
- *Weak:* it has no structure or time.

**B10. Trace waterfalls (Perfetto, Honeycomb, Jaeger)**
- Perfetto UI docs: https://flutter.googlesource.com/third_party/perfetto/+/refs/heads/canary/docs/visualization/perfetto-ui.md · Honeycomb waterfall: https://docs.honeycomb.io/reference/honeycomb-ui/query/trace-waterfall
- *Encodes:* thread or agent → track (row), span → bar on a time axis, parent-child → nesting. Perfetto *flow events* → arrows linking slices across tracks.
- *Works because:* this is the proven time-strip format for concurrency. Agent spawns and handoffs map exactly onto flow arrows.
- *Weak:* it has no code-space map, and it gets very tall with hundreds of agents unless tracks collapse.

**B11. Network attack maps (Kaspersky Cyberthreat Map, Norse)**: a *negative* exemplar
- Critique: https://www.csoonline.com/article/3217944/8-top-cyber-attack-maps-and-how-to-use-them.html · https://www.r-bloggers.com/2015/01/do-you-know-what-you-show-at-your-map/
- *Encodes:* attack source → destination as arcs over a globe.
- *Lesson:* this is the uncanny valley we are in. The arcs look like data, but CSO notes they are scan detections or honeypot subsets presented as "real-time", the maps are sales tools, and the rankings reflect detection footprint. Arcs with no quantitative meaning are exactly the parabolic-hop problem.

**B12. Context: Cursor "Scaling long-running autonomous coding"**, Wilson Lin et al., 2026-01
- https://cursor.com/blog/scaling-agents
- Not a visualization, but it describes the workload: hundreds of agents on one repo for weeks, planner, worker and judge hierarchy, and lock contention (20 agents performing like 2–3 under file locks). It suggests the panel should show *contention*: agents waiting on the same file or lock.

### C. Game and simulation UIs (many units over terrain)

**C1. Elite (1984) 3D elliptical scanner**, Braben & Bell; Elite: Dangerous (Frontier)
- Deep dive: https://elite.bbcelite.com/deep_dives/the_3d_scanner.html · ED forum (lines versus arcs): https://forums.frontier.co.uk/threads/sensor-improvement-arc-not-lines-to-show-objects-above-and-below.574754/latest
- *Encodes:* the ellipse is your horizontal plane. Each contact → dot plus a *stick* up or down to the plane, drawn in front of or behind the ellipse by sign.
- *Works because:* the stem turns an ambiguous projected point into an unambiguous (x, y, height) reading. This is the cheapest depth cue there is.
- *Weak:* players criticized ED's log scaling. Keep scales linear and labeled.

**C2. EVE Online tactical overlay**, CCP
- https://wiki.eveuniversity.org/Tactical_overlay · Images: https://wiki.eveuniversity.org/images/8/84/Tactical.jpeg, https://wiki.eveuniversity.org/images/9/9c/TacticalWithObjectSelected.jpeg · Brackets: https://www.eveonline.com/news/view/bracket-icon-feedback
- *Encodes:* a flat reference plane with range rings in km. The selected target gets a line plus an *arc projecting it onto the plane*. Velocity → arrow length. Weapon optimal and falloff → rings.
- *Works because:* range rings make distance readable without reading numbers. In the bracket design, player icons were kept as light wireframes so large crowds stay legible, while NPCs are filled.
- *Weak:* the overlay's brightness washes out the scene (player complaint). Use low-luminance ground rules.

**C3. Homeworld sensors manager and tactical overlay**, Relic, 1999; Remastered; Homeworld 3
- Manual (Cataclysm): https://homeworldarchives.com/media/Homeworld-Manual-Cataclysm.pdf · HW3 dev update: https://www.homeworlduniverse.com/war-games-feedback/ · Overlay mod notes: https://steamcommunity.com/sharedfiles/filedetails/?id=455300741
- *Encodes:* the full battlespace as tactical dots with a symbol legend, and overlay detail levels from icons only up to area-effect spheres.
- *Works because:* there is a separate *strategic zoom* where units become symbols. HW3 turned off icon amalgamation so every unit stays visible in dense clusters, which counts as data honesty. (I found no primary source describing its drop-line or ground-plane rendering; the general practice is documented for STK below.)
- *Weak:* the mod community notes clutter late in a game from area spheres.

**C4. STK (AGI) drop lines**: the engineering reference for stems
- https://help.agi.com/stk/Content/vo/droplines.htm
- *Encodes:* a line from an object down to MSL, terrain or the ellipsoid, drawn as a single drop or at intervals along a path.
- *Works because:* *interval* drop lines along a trajectory make a "curtain" that shows altitude history. That is a direct pattern for showing an agent's recent hops.

**C5. StarCraft II observer UI (production tab, Units Lost, GameHeart)**, Blizzard and the community
- https://news.blizzard.com/en-us/article/424210/press-the-replay-button · https://www.pcgamer.com/uk/custom-starcraft-ii-observer-interfaces-coming-in-heart-of-the-swarm · GameHeart: https://www.cyberpowerpc.com/blog/wcs-gameheart-popular-starcraft-observing-overlay-gets-official/
- *Encodes:* per-player resources, supply, income, a *production* tab (everything being built, with progress bars), army value and units lost.
- *Works because:* the map shows *where* and the tabs show *how much* and *what is in progress*. The production tab (a list of in-flight work with progress) is the right model for per-agent current tasks.
- *Weak:* it is broadcast-oriented and cycles fields over time ("time-sharing" the status bar).

**C6. ATC radar track displays (STARS/ERAM data blocks, history dots)**
- PPI tracks and labels: https://skyradar.com/blog/understanding-plots-tracks-and-labels-in-the-plan-position-indicator · STARS data block schema (leader-line direction field): https://www.fixm.aero/releases/US_Ext-4.4.0/doc/schema_documentation/Nas_starsDataBlockType.html · Configurable data tags: https://www.kongsberggeospatial.com/blog/configurable-complex-track-data-tag-formats-in-stars-erams-applications · Track features (symbol, leader line, label, velocity vector, history trails): https://virtualairtrafficsystem.com/docs/tracks/
- *Encodes:* aircraft → track symbol. A *data block* on a leader line carries call sign, altitude and speed, and its direction can be rotated per track. A few *history dots* show recent positions. A velocity vector shows the projection.
- *Works because:* it is decades of human-factors refinement for watching many actors for hours. Data blocks have priority ordering and time-share fields. Partial and full data blocks are a built-in LOD.
- *Weak:* the visual language is utilitarian. The relevant standards documents are FAA orders, which I did not fetch.

**C7. Aaron Koblin, *Flight Patterns***, 2005–2008 (Processing, FAA data)
- https://www.aaronkoblin.com/project/flight-patterns/ · Images: https://www.aaronkoblin.com/wp-content/uploads/2015/08/FP_01-US.jpg, https://www.aaronkoblin.com/wp-content/uploads/2015/08/hero-image3.jpg · Cooper Hewitt: https://www.cooperhewitt.org/2015/08/05/flight-patterns/
- *Encodes:* about 140k flight tracks over 24 hours → paths whose accumulated density forms the map. Color → aircraft type or altitude (per museum notes).
- *Works because:* *accumulation*. Individual tracks are faint, and the sum draws the structure (hubs, corridors). For us, many agents' hop trails summed over an hour would draw the "attention corridors" through the codebase.
- *Weak:* it is an art piece, so individuals are unreadable.

**C8. ADS-B Exchange / tar1090 live flight map**
- Map help: https://support.adsbexchange.com/hc/en-us/articles/44653064937741-Map-Help · Legend issue: https://adsbx.discourse.group/t/trail-color-not-shown-on-key/1146 · FR24 trail colors: https://support.fr24.com/support/solutions/articles/3000115027-why-does-the-aircraft-s-trail-change-colour
- *Encodes:* aircraft → icon colored on an altitude gradient (gray on the ground). Trail → past track, colored by altitude at each point.
- *Works because:* the trail color shows *history of a third variable* along the path, which is the cheapest persistent history there is.
- *Weak:* the legend is truncated at 40k ft (a known bug), a reminder that the legend must cover the full encoded range.

### D. High-information-density and time × structure exemplars

**D1. Marey / Ibry graphical train schedule**, Charles Ibry (attributed), published by É.-J. Marey 1885; Tufte's cover
- Bostock Observable: https://observablehq.com/notebook-kit/ex/d3/mareys-trains · Gist: https://gist.github.com/mbostock/5544008 · Plot version: https://observablehq.com/@observablehq/plot-mareys-trains · Attribution history: https://chance.amstat.org/?p=6922
- *Encodes:* y → stations at true distance, x → time of day, line → train, slope → speed, color → service type.
- *Works because:* every actor's *entire history* is visible at once, and overtakes and meetings are crossings. For agents, use a DFS order of the code tree on the y axis.
- *Weak:* a non-linear structure needs a 1D ordering, and the order choice dominates readability.

**D2. Storyline / narrative charts**, Randall Munroe (xkcd 657, 2009); Tanahashi & Ma (IEEE InfoVis 2012)
- https://xkcd.com/657/ · https://ieeevis.org/year/2012/paper/infovis/design-considerations-optimizing-storyline-visualizations · StoryFlow: https://shixialiu.com/publications/storyflow/index.html
- *Encodes:* character → line, time → x, co-location → lines converging into a band.
- *Works because:* it shows *who was with whom when*. Agents converging on one file form a visible braid.
- *Weak:* the layout optimization is expensive (evolutionary search). StoryFlow addresses this for interactivity.

**D3. Space-time cube (Hägerstrand time geography)**
- https://en.wikipedia.org/wiki/Time_geography · Immersive STC evaluation: https://arxiv.org/pdf/1908.00580
- *Encodes:* map plane (x, y) plus time on z. A path is a 3D polyline. A dwell is a vertical "station". Possible reach is a prism.
- *Works because:* height means something.
- *Weak:* occlusion and perspective. Usually you need rotation or a short time window.

**D4. Wind Map**, Fernanda Viégas & Martin Wattenberg, 2012 (hint.fm); successor Earth by Cameron Beccario
- https://panbo.com/hintfm-wind-map-genius/ · https://www.openculture.com/2013/02/artists_turn_weather_data_into_swirling_living_portraits_of_continental_us_wind_patterns_.html
- *Encodes:* the wind field → advected particle streaks, with density and brightness → speed.
- *Works because:* short *decaying trails* show direction and rate with no arrows or legend. It is calm enough to leave on all day.
- *Weak:* it encodes a field, not individual actors.

**D5. Sparklines**, Edward Tufte (2004 notebook; *Beautiful Evidence* 2006)
- https://www.edwardtufte.com/notebook/sparkline-theory-and-practice-edward-tufte/ · https://en.wikipedia.org/wiki/Sparkline
- *Encodes:* a word-sized time series placed inline with text or table cells.
- *Works because:* each agent's row in the data block or callout can carry a token-rate or tool-call sparkline at typographic size. It is a small multiple for free.
- *Weak:* there is no absolute scale unless you add end-point labels and min/max dots.

**D6. Bret Victor, "Up and Down the Ladder of Abstraction"**, 2011
- Coverage: https://waxy.org/2011/10/bret_victors_up/ (the essay is hosted at worrydream.com/LadderOfAbstraction/ per search results)
- *Encodes:* one system shown at increasing abstraction: a single run at time t, then the trajectory over all t, then a family of trajectories over a parameter, then a summary histogram.
- *Works because:* it is the design principle for the panel. Show the *instant* (glyph positions), *abstract over time* (trails and heat), and *abstract over agents* (aggregate corridors and histograms), all linked.
- *Weak:* it is an essay and a method, not a component.

**D7. Territory Studio, *The Martian* screen graphics**, 2015, a data-grounded FUI exemplar
- https://territorystudio.com/project/the-martian/ · Reel: https://vimeo.com/142143279 · Design Week: https://www.designweek.co.uk/for-new-film-the-martian-territory-takes-nasa-screens-30-years-into-the-future/
- *Relevance:* about 400 screens extrapolated from real NASA mission-control conventions, with input from NASA's Dave Lavery. It shows that FUI reads as credible when it extends real operational conventions (telemetry tables, plots, timelines) rather than inventing decoration.
- *Weak:* it is still film graphics, so its density is tuned for camera, not operators.

## Techniques we should steal (synthesis)

Ordered roughly by impact per unit of effort for the next mockup:

1. **Give the floor mass and keep it still.** Replace the evenly spaced tree with
   a stable squarified treemap or circle pack. Area = LOC, and low extrusion =
   one metric (churn or test coverage). Never re-layout while running; use an
   EvoCells-style stable layout on tree change. (CodeCharta, software maps,
   CodeCity, Kuhn's consistent layout.)
2. **Make the floor remember.** Each node accumulates a touch state (unvisited →
   read → edited, as in Mindwalk) plus a heat value that decays slowly. Use warm
   for mutation and cool for observation. Nodes deleted during the session
   linger as ghosts. (Mindwalk, SeeSoft, Flight Patterns accumulation.)
3. **Spend height on data or drop it.** Either z = time (a space-time cube over
   a rolling window, with dwell drawn as vertical stations) or z = 0 with motion
   on the floor. If glyphs are ever airborne, draw a **stem** to the floor and a
   faint shadow. (Hägerstrand, Elite, EVE, STK.)
4. **Move along structure, not through the air.** Route each hop along the tree
   path to the lowest common ancestor and back down (Holten bundling), so the
   path itself says "left src/, went into tests/". Animate a comet head with a
   decaying tail. (Holten, Vizceral, Wind Map.)
5. **Labels ride with actors as ATC data blocks.** Use a two-line block on a
   rotatable leader line: `sa·fuzz  edit` / `raw_reader.cpp:seek  12.4k▲`.
   Collapse to a partial block (id only) when quiet and expand on event or
   focus. Add 3–5 history dots behind each glyph. Drop the separate callout
   column, or keep it only as the sorted index. (STARS/ERAM, Gource, Smallville.)
6. **Make events punctuate.** An edit gets a brief ring or ripple on the node,
   and a read gets a dimmer pulse. A conflict (two agents in one file, one
   editing) gets a persistent red bracket until it resolves. A blocked or
   waiting agent gets a distinct, steady (not animated) marker, because the
   operator's most important question is "who needs me?". (Gource flashes,
   codemap/Pixel Agents blocked state, Logstalgia hit or miss.)
7. **Add a time strip that carries real history.** On the portrait panel, put a
   Marey or storyline strip under the map: y = DFS order of the same tree (with
   colors keyed to the treemap), x = time, one polyline per agent, and glyph
   marks for spawn (○), compaction (◇) and user turn (›). Convergent lines show
   contention, and flat lines show stalls. Add flow arrows from parent to child
   on spawn. (Marey, xkcd 657, Mindwalk timeline, Perfetto flows.)
8. **Use small multiples at the margin.** Give each agent a kube-ops-view-style
   tile with a token-rate sparkline, a tool mix bar, an age counter and a state
   border color, sorted by need-for-attention. (kube-ops-view, Tufte sparklines,
   SC2 production tab.)
9. **Make density honest.** Never merge glyphs silently. HW3 reversed icon
   amalgamation, and EVE kept crowds light with wireframes. Show the true count,
   and when you must aggregate, label the aggregate with its N.
10. **Avoid the attack-map trap.** If a motion, arc, glow or particle does not
    map to a datum someone could check, remove it. That is the operator's
    "information-carrying, never decorative-only" rule, and it is the most
    likely root of the current uncanniness.

## The "Hundreds of agents cross together" image

**Not identified.** I searched the exact phrases ("Hundreds of agents cross
together", "Everything known", "New ground") alone and in combination, with
agent, swarm, frontier and landing-page context, across about 10 queries. I also
checked the figure lists of the most plausible 2026 sources:

- Cursor, "Scaling long-running autonomous coding" (https://cursor.com/blog/scaling-agents): one figure, a PR screenshot. No match.
- Dwarkesh, "The Rise and Fall of Agent Civilizations" (https://www.dwarkesh.com/p/openai-huggingface): no figures. No match.
- METR's incident investigation (https://metr.org/blog/2026-08-26-openai-hugging-face-incident-investigation/): 13 figures, none captioned with the phrases. Text inside the images was not inspected.
- Gusev & Bernal Neira, "AI Agent Swarms as Researchers" (arXiv 2609.35719, https://arxiv.org/html/2609.35719): Figure 1 is a procedure diagram. No match in the text.
- Google's "ScientistTwo" (arXiv 2609.19644, https://arxiv.org/html/2609.19644v1): its Figure 1 teaser is about "pushing the frontier of human knowledge", which is thematically close, but the caption does not use the phrases.

I also searched the local Blackbird repository for the phrases and found
nothing. Leads for the operator: the two-plane framing ("Everything known" below,
"New ground" above, agents crossing between them) resembles Matt Might's
"Illustrated guide to a Ph.D." circle-of-knowledge idiom. It could come from an
AI-for-science lab's landing-page hero animation, which search engines index
poorly. If the operator has the image file, its EXIF data, a reverse image
search, or the page it was copied from would settle it.

## Method notes

- About 45 web searches and 20 page fetches. No binaries were downloaded, and
  only links are recorded.
- Gaps: I found no primary source for Homeworld's ground-plane or drop-line
  rendering, and I could not fetch the D3 hierarchical-edge-bundling notebook
  (HTTP 429), so it is not cited. Treat the details marked "(unverified)" as
  hypotheses.
