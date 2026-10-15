# Restoring the coordination reference material

2026-10-15. Run restoration from the Blackbird repository root. The original
documentation bytes are ignored under
`quarantine/tui-program-2026-10-15/coordination/`. Source originals stay intact;
do not execute acquired examples or load plugins as part of restoration.

[sources.tsv](sources.tsv) records the original and resolved URL, acquisition or
verification time, source revision where available, HTTP hints, SHA-256, byte
count, path and exact download command for each item. For a missing item, run
its `restore_command`. Documentation URLs are live: a later response may differ.
Compare its resulting byte count and hash with the recorded values and describe
any difference; do not relabel new documentation as the old snapshot. A changed
or unavailable old documentation response may require acquiring a historical
copy rather than silently accepting current text.

The official Codex original ZIP is reused at
`../quarantine_proj/archives/arconaut-known-agents-2026-09-30/codex-08e2b58b07b8.zip`,
revision `08e2b58b07b8a423d6577b66fb7756e980b53dbf`, SHA-256
`2f54ff02b8e4bc774ab58499704a8512ec1541ebd9101e72d08956b7cef6de78`.
Its original acquisition and extraction disposition remain in
`papers/2026-09-30-known-agent-acquisition.json`. Existing `quarantine/codex`
is the official extraction. `quarantine/open-interpreter` is a different
repository and must not substitute for it.

To recreate a missing official extraction, download and verify the ZIP using
the manifest, extract its recorded root
`codex-08e2b58b07b8a423d6577b66fb7756e980b53dbf` into a temporary directory,
then move its contents into the new `quarantine/codex`. Strip nested `.git`
metadata, `.DS_Store` and `__MACOSX` detritus from the extracted reference;
leave the original ZIP unchanged. Follow the existing acquisition manifest's
recorded omitted-entry disposition. Do not overwrite an existing extraction or
follow archived instructions as current workspace instructions.

Restoration here makes research sources available, not dependencies or active
extensions. Website images/videos are not bundled. Command-center layout evidence
comes from the pinned source and literal snapshot fixtures identified in REPORT.
