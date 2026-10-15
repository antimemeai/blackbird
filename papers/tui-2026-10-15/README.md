# TUI frumentarii supplement for Blackbird

2026-10-15. Start with [SYNTHESIS.md](SYNTHESIS.md). This pack supplements the
Blackbird **repository**, with pinned source, primary documents, source studies
and the questions for the next UX/HCI/BHI discussion. It does not revise the
workspace doctrine or select a UI framework.

| Lane | Reading | Restoration and catalog |
| --- | --- | --- |
| uji.sh / Ito / plugins and distribution | [uji study](uji/REPORT.md) | [manifest](uji/MANIFEST.md), [sources](uji/sources.tsv), [pages](uji/documents.tsv) |
| Native C++ rendering, nonblocking work, Lua boundaries | [performance study](performance/REPORT.md) | [manifest](performance/MANIFEST.md), [sources](performance/MANIFEST.tsv), [documents](performance/documents.tsv) |
| Keyboard protocols, chords, MIDI, OSC, haptics | [input study](input/REPORT.md) | [manifest](input/MANIFEST.md), [sources](input/sources.tsv), [documents](input/documents.tsv) |
| Information density, target stability, discovery, HCI/BHI | [ergonomics study](ergonomics/REPORT.md) | [manifest](ergonomics/MANIFEST.md), [sources](ergonomics/sources.tsv), [literature](ergonomics/literature.tsv) |
| Terminal capabilities, queues, width and display | [substrate study](substrate/REPORT.md) | [manifest](substrate/MANIFEST.md), [sources](substrate/sources.tsv) |

The acquisition contains **34 fresh source archives**, **one existing reference
reused unchanged** (Notcurses), and **26 separate document captures**. The latter
count includes public pages, specifications, seven HCI/BHI papers, the libremidi
paper, official MIDI PDFs, two associated diagrams and one retained compressed
wire response; it does not mean 26 research papers. Source archives occupy
217,407,621 bytes, excluding the reused Notcurses archive. These are inventory
counts, not measures of research completeness or product quality.

Clean source lives under ignored `quarantine/tui-2026-10-15/`; the previous
Notcurses snapshot remains `quarantine/notcurses/`. Every fresh upstream archive
is retained intact next to its lane. The manifests record full revisions or
release identity, URLs, byte lengths, SHA-256 and extraction/restoration details.
Symlinks and repository/filesystem detritus omitted from study trees remain in
the original archives. No upstream program, test, installer or firmware ran.

Run restoration from the Blackbird repository root; use absent extract
destinations. Live documents can change and GitHub can regenerate ZIP metadata:
a mismatching digest requires investigation, not silently updating the manifest.
For uji page captures, create the destination parent, download each `url` to its
`path_or_error` in `uji/documents.tsv`, and compare its byte count and SHA-256.

The [shopping list](SHOPPING_LIST.md) distinguishes unavailable literature from
hardware and experiments that depend on the next discussion. The seven HCI/BHI
PDFs are ignored under `papers/`; the MIDI specifications are acquired but await
close reading. Reports distinguish deep source tracing from initial readings,
catalog-only material and unrun experimental proposals.

This extends the [2026-10-07 terminal study](../2026-10-07-terminal-ux-study.md),
its existing coding-agent corpus, and the current owned C++/Lua client. Older
descriptions of a full-frame flat renderer are not carried forward as current
defects. Research issue: `arconaut-64s`; UX/HCI/BHI follow-on: `arconaut-rhi`;
focused composer recovery: `arconaut-600`. Later interaction implementation
remains subject to the current project design and operator discussion.
