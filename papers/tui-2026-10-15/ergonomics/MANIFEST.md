# Ergonomics source acquisition manifest

Recorded UTC: 2026-10-15T14:24:04.669244+00:00

Nine upstream ZIPs are retained intact under ignored `quarantine/tui-2026-10-15/ergonomics/archives/`. Each repository HEAD was resolved to a full commit, then downloaded from GitHub codeload at that commit. `sources.tsv` is the repository-root-relative external-source catalog: source/download URLs, full revisions, archive and extract paths, archive bytes and SHA-256, retained file/byte counts, archive roots, omitted entries and failures. The original helper `sources.json` input and `acquisitions.json` result remain under ignored `quarantine/tui-2026-10-15/ergonomics/`. Download completion UTC in the TSV is observed from each intact archive's filesystem modification time. No acquired program was built, installed or executed.

Extraction used the existing `scripts/acquire_references.py` and `scripts/ingest_zip.py`. They reject unsafe archive paths and special entries, omit nested Git metadata, macOS/Python detritus, `.entire`, `._*` and symlinks; they directly compare retained file sets, bytes and executable bits with each intact source archive. Symlink omission is recorded and means these snapshots are study trees, not promised buildable distributions. No acquired source files were edited.

| Reference | Revision | Source ZIP bytes | Retained files | Omitted file entries | Root license notice |
| --- | --- | ---: | ---: | ---: | --- |
| [bottom](https://github.com/ClementTsang/bottom) | `7d65d62c8310f592727e6a142007f761504c2c8b` | 3,753,158 | 371 | 0 | MIT; `LICENSE` |
| [btop](https://github.com/aristocratos/btop) | `119c8d83e1331ac6fbc9aef0fe78c7fadda9b391` | 1,365,605 | 145 | 0 | Apache-2.0; `LICENSE` |
| [fzf](https://github.com/junegunn/fzf) | `c8c2d0828a94a00879ceb6f95112a42e3ea322cf` | 562,606 | 163 | 0 | MIT; `LICENSE` |
| [harlequin](https://github.com/tconbeer/harlequin) | `33f63fdfa12c0d6b69279f621de72e3a797a570a` | 2,181,303 | 449 | 0 | MIT; `LICENSE` |
| [helix](https://github.com/helix-editor/helix) | `ba40e547426b0f9896c8bdc699a4ab11f2b37dbc` | 3,775,593 | 2,013 | 1 | MPL-2.0; `LICENSE` |
| [kakoune](https://github.com/mawww/kakoune) | `4d4ee86c2d039c2a5f29f109e7e59105d2073046` | 1,859,269 | 2,659 | 9 | Unlicense; `UNLICENSE` |
| [lnav](https://github.com/tstack/lnav) | `753b61bf9c4987342a3f23000ed2a466187d4429` | 37,615,475 | 4,585 | 1 | BSD-2-Clause; `LICENSE` |
| [tig](https://github.com/jonas/tig) | `5a8fd507cd7e866833fa029fa7c7c098736a72c2` | 1,241,088 | 317 | 0 | GPL-2.0-or-later (src/tig.c header); GPL-2.0 text; `COPYING` |
| [yazi](https://github.com/sxyazi/yazi) | `ffb2e78cc70341946c04befa52e14a4494015ce8` | 1,886,882 | 1,501 | 0 | MIT; separate MIT icons notice; `LICENSE; LICENSE-ICONS` |

Licenses describe the inspected root notice, not a legal inventory of every bundled third-party file. The original archives retain all notices. Acquisition is reference study, not dependency adoption.

## Exact source restoration

Run these commands from the Blackbird repository root on a fresh destination. They use pinned upstream bytes and the current owned ingestion helper. The catalog contains the expected archive digest and exact archive root. The helper refuses existing extract destinations.

### bottom

SHA-256: `c97f6fa8349123ff2dd842552a79e73d6149cbce7ac1dd467eadb8a8e24ad786`.

```sh
mkdir -p quarantine/tui-2026-10-15/ergonomics/archives
curl --disable --fail --location --retry 2 --output quarantine/tui-2026-10-15/ergonomics/archives/bottom-7d65d62c8310.zip https://codeload.github.com/ClementTsang/bottom/zip/7d65d62c8310f592727e6a142007f761504c2c8b
python3 scripts/ingest_zip.py quarantine/tui-2026-10-15/ergonomics/archives/bottom-7d65d62c8310.zip quarantine/tui-2026-10-15/ergonomics/bottom --root bottom-7d65d62c8310f592727e6a142007f761504c2c8b --skip-symlinks
```

### btop

SHA-256: `9073a6cbf6a5002aa753b71c3446a67f6a22dab7aab44c77e7b4b5cddbb4a9f3`.

```sh
mkdir -p quarantine/tui-2026-10-15/ergonomics/archives
curl --disable --fail --location --retry 2 --output quarantine/tui-2026-10-15/ergonomics/archives/btop-119c8d83e133.zip https://codeload.github.com/aristocratos/btop/zip/119c8d83e1331ac6fbc9aef0fe78c7fadda9b391
python3 scripts/ingest_zip.py quarantine/tui-2026-10-15/ergonomics/archives/btop-119c8d83e133.zip quarantine/tui-2026-10-15/ergonomics/btop --root btop-119c8d83e1331ac6fbc9aef0fe78c7fadda9b391 --skip-symlinks
```

### fzf

SHA-256: `24433add376f8693563eac254db4af35346171b6d8a04c1fe5dc73b6942f82f7`.

```sh
mkdir -p quarantine/tui-2026-10-15/ergonomics/archives
curl --disable --fail --location --retry 2 --output quarantine/tui-2026-10-15/ergonomics/archives/fzf-c8c2d0828a94.zip https://codeload.github.com/junegunn/fzf/zip/c8c2d0828a94a00879ceb6f95112a42e3ea322cf
python3 scripts/ingest_zip.py quarantine/tui-2026-10-15/ergonomics/archives/fzf-c8c2d0828a94.zip quarantine/tui-2026-10-15/ergonomics/fzf --root fzf-c8c2d0828a94a00879ceb6f95112a42e3ea322cf --skip-symlinks
```

### harlequin

SHA-256: `af7dce5aa46bf76b698191024188bc16184cb0fae81206263ad96901862dbb21`.

```sh
mkdir -p quarantine/tui-2026-10-15/ergonomics/archives
curl --disable --fail --location --retry 2 --output quarantine/tui-2026-10-15/ergonomics/archives/harlequin-33f63fdfa12c.zip https://codeload.github.com/tconbeer/harlequin/zip/33f63fdfa12c0d6b69279f621de72e3a797a570a
python3 scripts/ingest_zip.py quarantine/tui-2026-10-15/ergonomics/archives/harlequin-33f63fdfa12c.zip quarantine/tui-2026-10-15/ergonomics/harlequin --root harlequin-33f63fdfa12c0d6b69279f621de72e3a797a570a --skip-symlinks
```

### helix

SHA-256: `87d2cb923650e2aad773592f3ea4f963717efcceac3e5f93efd239ed9d40a860`.

```sh
mkdir -p quarantine/tui-2026-10-15/ergonomics/archives
curl --disable --fail --location --retry 2 --output quarantine/tui-2026-10-15/ergonomics/archives/helix-ba40e547426b.zip https://codeload.github.com/helix-editor/helix/zip/ba40e547426b0f9896c8bdc699a4ab11f2b37dbc
python3 scripts/ingest_zip.py quarantine/tui-2026-10-15/ergonomics/archives/helix-ba40e547426b.zip quarantine/tui-2026-10-15/ergonomics/helix --root helix-ba40e547426b0f9896c8bdc699a4ab11f2b37dbc --skip-symlinks
```

### kakoune

SHA-256: `694a942ae181288e8b06aef019c81ba9bb37d9912679187ca397ff9d4c527f90`.

```sh
mkdir -p quarantine/tui-2026-10-15/ergonomics/archives
curl --disable --fail --location --retry 2 --output quarantine/tui-2026-10-15/ergonomics/archives/kakoune-4d4ee86c2d03.zip https://codeload.github.com/mawww/kakoune/zip/4d4ee86c2d039c2a5f29f109e7e59105d2073046
python3 scripts/ingest_zip.py quarantine/tui-2026-10-15/ergonomics/archives/kakoune-4d4ee86c2d03.zip quarantine/tui-2026-10-15/ergonomics/kakoune --root kakoune-4d4ee86c2d039c2a5f29f109e7e59105d2073046 --skip-symlinks
```

### lnav

SHA-256: `9dbd6dbe24a24ba7a6aeac9a2d66a4a2402ec957eb53c7a0d6d50a8191fd200a`.

```sh
mkdir -p quarantine/tui-2026-10-15/ergonomics/archives
curl --disable --fail --location --retry 2 --output quarantine/tui-2026-10-15/ergonomics/archives/lnav-753b61bf9c49.zip https://codeload.github.com/tstack/lnav/zip/753b61bf9c4987342a3f23000ed2a466187d4429
python3 scripts/ingest_zip.py quarantine/tui-2026-10-15/ergonomics/archives/lnav-753b61bf9c49.zip quarantine/tui-2026-10-15/ergonomics/lnav --root lnav-753b61bf9c4987342a3f23000ed2a466187d4429 --skip-symlinks
```

### tig

SHA-256: `f42550c3d0aeb4a8741abfc5417845d2564a9aa278b859575869dafa77bc5ed7`.

```sh
mkdir -p quarantine/tui-2026-10-15/ergonomics/archives
curl --disable --fail --location --retry 2 --output quarantine/tui-2026-10-15/ergonomics/archives/tig-5a8fd507cd7e.zip https://codeload.github.com/jonas/tig/zip/5a8fd507cd7e866833fa029fa7c7c098736a72c2
python3 scripts/ingest_zip.py quarantine/tui-2026-10-15/ergonomics/archives/tig-5a8fd507cd7e.zip quarantine/tui-2026-10-15/ergonomics/tig --root tig-5a8fd507cd7e866833fa029fa7c7c098736a72c2 --skip-symlinks
```

### yazi

SHA-256: `e3ece74b12089ad72035260df1dc1f8c8bba1089f643d4cb524e5d3d2aa2e571`.

```sh
mkdir -p quarantine/tui-2026-10-15/ergonomics/archives
curl --disable --fail --location --retry 2 --output quarantine/tui-2026-10-15/ergonomics/archives/yazi-ffb2e78cc703.zip https://codeload.github.com/sxyazi/yazi/zip/ffb2e78cc70341946c04befa52e14a4494015ce8
python3 scripts/ingest_zip.py quarantine/tui-2026-10-15/ergonomics/archives/yazi-ffb2e78cc703.zip quarantine/tui-2026-10-15/ergonomics/yazi --root yazi-ffb2e78cc70341946c04befa52e14a4494015ce8 --skip-symlinks
```

## Literature

Seven primary-source PDFs are in this papers directory and remain ignored. `literature.tsv` uses the Blackbird repository root for paths/restoration commands and records title, source URL, revision/publication year, download UTC, byte count, SHA-256, local path, source reuse notice, exact curl restoration command and failures. The university/Microsoft PDFs are pinned by retained hash; their URLs are mutable. The SWE-agent arXiv URL explicitly names v3.

Derived text lives under ignored `quarantine/tui-2026-10-15/ergonomics/literature-text/`; restore with `pdftotext -layout INPUT.pdf OUTPUT.txt`. The distributed cognition PDF is an image scan with no text layer; its empty derived text is expected. Its opening three pages were inspected as images.

## Failures and limits

All nine source ZIPs and seven PDFs downloaded successfully. No fallback checkout was needed. Web parsing of the Kakoune GitHub landing page and distributed cognition PDF returned internal errors; source acquisition and local PDF inspection succeeded. No latency benchmarks, user studies, acquired tests, accounts, reference programs or installers were run. Additional literature sought without a downloadable authoritative copy is listed in `SHOPPING_LIST.md`.
