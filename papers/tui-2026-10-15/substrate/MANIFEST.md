# Source manifest and restoration

Acquired 2026-10-15T14:26:11.334861+00:00. Intact source ZIPs and clean extracts are ignored; this manifest is tracked. Source study only. No dependency selected, acquired executable run, or account connected.

Run from the Blackbird repository root with an absent extraction destination. The owned ingestion tool omits Git metadata, filesystem detritus and symbolic links. Original ZIP bytes remain intact. The TSV records omitted entries; upstream instructions remain historical source material.

GitHub commit URLs pin source identities. Future server-generated ZIP compression/metadata can change: if a restored ZIP hash differs, retain the new download separately and investigate rather than silently replacing this record.

## ghostty

- Repository: https://github.com/ghostty-org/ghostty
- Revision: `22fa134a07f5749edd9e92c6bc17d8aaf61843be`
- SHA-256: `74a3e6d2b2b1d11ae43fada8e72ab21b6704c000f35a4927a88ac93f1b1544b7`; ZIP bytes: 42,391,422; retained files: 5,911.
- Extract: `quarantine/tui-2026-10-15/substrate/source/ghostty`; excluded entries: 0.

```sh
mkdir -p quarantine/tui-2026-10-15/substrate/archives
curl --disable --fail --location --output quarantine/tui-2026-10-15/substrate/archives/ghostty-22fa134a07f5.zip https://codeload.github.com/ghostty-org/ghostty/zip/22fa134a07f5749edd9e92c6bc17d8aaf61843be
printf '%s  %s\n' 74a3e6d2b2b1d11ae43fada8e72ab21b6704c000f35a4927a88ac93f1b1544b7 quarantine/tui-2026-10-15/substrate/archives/ghostty-22fa134a07f5.zip | shasum -a 256 -c -
python3 scripts/ingest_zip.py quarantine/tui-2026-10-15/substrate/archives/ghostty-22fa134a07f5.zip quarantine/tui-2026-10-15/substrate/source/ghostty --root ghostty-22fa134a07f5749edd9e92c6bc17d8aaf61843be --skip-symlinks
```

## libvaxis

- Repository: https://github.com/rockorager/libvaxis
- Revision: `d4c3b3666e0c090e2b5092ca09893c6db15e0b79`
- SHA-256: `7eec8e64ae3a04a4e5e20cd6870a29f8c220724bd61f9890ccf62d3d967174b9`; ZIP bytes: 1,318,928; retained files: 89.
- Extract: `quarantine/tui-2026-10-15/substrate/source/libvaxis`; excluded entries: 0.

```sh
mkdir -p quarantine/tui-2026-10-15/substrate/archives
curl --disable --fail --location --output quarantine/tui-2026-10-15/substrate/archives/libvaxis-d4c3b3666e0c.zip https://codeload.github.com/rockorager/libvaxis/zip/d4c3b3666e0c090e2b5092ca09893c6db15e0b79
printf '%s  %s\n' 7eec8e64ae3a04a4e5e20cd6870a29f8c220724bd61f9890ccf62d3d967174b9 quarantine/tui-2026-10-15/substrate/archives/libvaxis-d4c3b3666e0c.zip | shasum -a 256 -c -
python3 scripts/ingest_zip.py quarantine/tui-2026-10-15/substrate/archives/libvaxis-d4c3b3666e0c.zip quarantine/tui-2026-10-15/substrate/source/libvaxis --root libvaxis-d4c3b3666e0c090e2b5092ca09893c6db15e0b79 --skip-symlinks
```
