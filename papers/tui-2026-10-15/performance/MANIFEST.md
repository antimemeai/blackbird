# Performance/reference acquisition manifest

Recorded 2026-10-15. Research only; nothing built, imported into production, or selected as a dependency.

[MANIFEST.tsv](MANIFEST.tsv) records each upstream, immutable Git revision (or release version and exact byte hash), original archive path/size/SHA256, cleaned extraction, license notices, restoration command and omitted entries. [documents.tsv](documents.tsv) records four downloaded primary specifications. All relative paths there start at the Blackbird repository root.

Nine fresh GitHub ZIPs and the official libtermkey 0.22 tarball were retained intact, and their retained extracted bytes were compared directly to archive members. Extraction omits nested `.git`, filesystem detritus, and symlinks using the existing ingestion machinery. The source files were never edited. No upstream instructions were applied as workspace instructions. Symlink omissions matter: vis has 14, FTXUI has one; intact archives preserve them. This is a reading corpus, not a buildable dependency lockfile. GitHub ZIPs also do not recursively populate Git submodules.

The pinned Notcurses snapshot already existed at `quarantine/notcurses`; its old tracked acquisition manifest is [2026-10-07-render-reference-acquisition.json](../../2026-10-07-render-reference-acquisition.json). It was reused unchanged. A newly fetched duplicate at the identical revision was removed from this lane. FTXUI's newer pin changes the application/event-loop machinery, so both old and new snapshots serve comparison.

Restore from the Blackbird root into absent destinations: fetch each `download_url` to its `archive` path, verify its SHA256 from the TSV with `shasum -a 256`, then run `restore_command`. ZIP commands use the owned `scripts/ingest_zip.py` and preserve source executable bits. The official libtermkey tarball was inspected before extraction: one top-level directory, no absolute/traversing paths, no links or special files and no detritus; its TSV `tar` command restores this exact inspected artifact. Verify its expected digest first. HTML documentation files restore directly with `curl --disable --fail --location --output RETAINED_FILE SOURCE_URL`; verify the corresponding digest. Live documentation URLs may change bytes; hashes describe the retained snapshot, not a promise that upstream always serves it.

Acquisition failures and resolution:

- `https://github.com/leonerd/libtermkey` did not resolve publicly with authentication disabled. The canonical upstream describes Bazaar source and discourages new use of standalone libtermkey. Acquired the official 0.22 source tarball instead; no fork substituted.
- The initial unverified guess `https://github.com/jugglerchris/luatermfx` did not resolve publicly. Replaced this candidate with verified `lunarmodules/terminal.lua`; no termfx acquisition is claimed.
- Python's system TLS failed against leonerd.org.uk. System `curl` succeeded without weakening TLS.
- Web PDF/download parser could not ingest libtermkey's binary archive and newer Open Group `write` URL; direct curl acquired the former and the published POSIX.1-2017 `write` document. No PDF was required for this lane.

The helper's raw upstream records, including the failed URL attempts, remain ignored at `quarantine/tui-2026-10-15/performance/acquisition-catalog.json`; manual tar record is `libtermkey-acquisition.json`. The TSV is the compact reconstruction manifest for the actual acquired set.
