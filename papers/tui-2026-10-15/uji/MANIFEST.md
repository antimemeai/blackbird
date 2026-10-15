# Source manifest and restoration

Acquired 2026-10-15T14:24:32.716387+00:00. Intact source ZIPs and clean extracts are ignored; this manifest is tracked. Source study only. No dependency selected, acquired executable run, or account connected.

Run from the Blackbird repository root with an absent extraction destination. The owned ingestion tool omits Git metadata, filesystem detritus and symbolic links. Original ZIP bytes remain intact. The TSV records omitted entries; upstream instructions remain historical source material.

GitHub commit URLs pin source identities. Future server-generated ZIP compression/metadata can change: if a restored ZIP hash differs, retain the new download separately and investigate rather than silently replacing this record.

## copr

- Repository: https://github.com/uji-labs/copr
- Revision: `cf61946f68b059e8ac8f50d36ac0f2309e8de233`
- SHA-256: `badf4266daa5d520b1664474b9b1d0c711a9ccdcc02e8e18602d8a2e4101ec2c`; ZIP bytes: 10,062; retained files: 9.
- Extract: `quarantine/tui-2026-10-15/uji/source/copr`; excluded entries: 0.

```sh
mkdir -p quarantine/tui-2026-10-15/uji/archives
curl --disable --fail --location --output quarantine/tui-2026-10-15/uji/archives/copr-cf61946f68b0.zip https://codeload.github.com/uji-labs/copr/zip/cf61946f68b059e8ac8f50d36ac0f2309e8de233
printf '%s  %s\n' badf4266daa5d520b1664474b9b1d0c711a9ccdcc02e8e18602d8a2e4101ec2c quarantine/tui-2026-10-15/uji/archives/copr-cf61946f68b0.zip | shasum -a 256 -c -
python3 scripts/ingest_zip.py quarantine/tui-2026-10-15/uji/archives/copr-cf61946f68b0.zip quarantine/tui-2026-10-15/uji/source/copr --root copr-cf61946f68b059e8ac8f50d36ac0f2309e8de233 --skip-symlinks
```

## homebrew-uji

- Repository: https://github.com/uji-labs/homebrew-uji
- Revision: `ef3025f317a413f7bb05d812f4188ddac5652254`
- SHA-256: `fd73e0ca35f05f5f35a662ba2f9b0b6a397e8278c2bda0cccf3129937ba9b15a`; ZIP bytes: 4,446; retained files: 5.
- Extract: `quarantine/tui-2026-10-15/uji/source/homebrew-uji`; excluded entries: 0.

```sh
mkdir -p quarantine/tui-2026-10-15/uji/archives
curl --disable --fail --location --output quarantine/tui-2026-10-15/uji/archives/homebrew-uji-ef3025f317a4.zip https://codeload.github.com/uji-labs/homebrew-uji/zip/ef3025f317a413f7bb05d812f4188ddac5652254
printf '%s  %s\n' fd73e0ca35f05f5f35a662ba2f9b0b6a397e8278c2bda0cccf3129937ba9b15a quarantine/tui-2026-10-15/uji/archives/homebrew-uji-ef3025f317a4.zip | shasum -a 256 -c -
python3 scripts/ingest_zip.py quarantine/tui-2026-10-15/uji/archives/homebrew-uji-ef3025f317a4.zip quarantine/tui-2026-10-15/uji/source/homebrew-uji --root homebrew-uji-ef3025f317a413f7bb05d812f4188ddac5652254 --skip-symlinks
```

## ito

- Repository: https://github.com/uji-labs/ito
- Revision: `08c6a5ac8bdf72f3595db17cf2c32a10edca3f9f`
- SHA-256: `195d1fe8eb4514fafe7a457db823a66c0a75862923dd787aaf7e1f5820f7616b`; ZIP bytes: 206,901; retained files: 159.
- Extract: `quarantine/tui-2026-10-15/uji/source/ito`; excluded entries: 0.

```sh
mkdir -p quarantine/tui-2026-10-15/uji/archives
curl --disable --fail --location --output quarantine/tui-2026-10-15/uji/archives/ito-08c6a5ac8bdf.zip https://codeload.github.com/uji-labs/ito/zip/08c6a5ac8bdf72f3595db17cf2c32a10edca3f9f
printf '%s  %s\n' 195d1fe8eb4514fafe7a457db823a66c0a75862923dd787aaf7e1f5820f7616b quarantine/tui-2026-10-15/uji/archives/ito-08c6a5ac8bdf.zip | shasum -a 256 -c -
python3 scripts/ingest_zip.py quarantine/tui-2026-10-15/uji/archives/ito-08c6a5ac8bdf.zip quarantine/tui-2026-10-15/uji/source/ito --root ito-08c6a5ac8bdf72f3595db17cf2c32a10edca3f9f --skip-symlinks
```

## uji

- Repository: https://github.com/uji-labs/uji
- Revision: `9a612f15a772921867f8df688e1630f2ae6aa834`
- SHA-256: `d0d9ebba25f6577ad3c00e7731f9cd1a7916304446e58698e92ddef4ba3f1a1d`; ZIP bytes: 15,070,666; retained files: 474.
- Extract: `quarantine/tui-2026-10-15/uji/source/uji`; excluded entries: 1.

```sh
mkdir -p quarantine/tui-2026-10-15/uji/archives
curl --disable --fail --location --output quarantine/tui-2026-10-15/uji/archives/uji-9a612f15a772.zip https://codeload.github.com/uji-labs/uji/zip/9a612f15a772921867f8df688e1630f2ae6aa834
printf '%s  %s\n' d0d9ebba25f6577ad3c00e7731f9cd1a7916304446e58698e92ddef4ba3f1a1d quarantine/tui-2026-10-15/uji/archives/uji-9a612f15a772.zip | shasum -a 256 -c -
python3 scripts/ingest_zip.py quarantine/tui-2026-10-15/uji/archives/uji-9a612f15a772.zip quarantine/tui-2026-10-15/uji/source/uji --root uji-9a612f15a772921867f8df688e1630f2ae6aa834 --skip-symlinks
```

## uji-plugins

- Repository: https://github.com/uji-labs/uji-plugins
- Revision: `7edceeec13a679edf216a7ae6c2fa6a4bf084c25`
- SHA-256: `cd689f6d1a3e1e39fb5373d0c3e7f26acb96017596b65c6cbc15150d68d26079`; ZIP bytes: 44,937; retained files: 29.
- Extract: `quarantine/tui-2026-10-15/uji/source/uji-plugins`; excluded entries: 0.

```sh
mkdir -p quarantine/tui-2026-10-15/uji/archives
curl --disable --fail --location --output quarantine/tui-2026-10-15/uji/archives/uji-plugins-7edceeec13a6.zip https://codeload.github.com/uji-labs/uji-plugins/zip/7edceeec13a679edf216a7ae6c2fa6a4bf084c25
printf '%s  %s\n' cd689f6d1a3e1e39fb5373d0c3e7f26acb96017596b65c6cbc15150d68d26079 quarantine/tui-2026-10-15/uji/archives/uji-plugins-7edceeec13a6.zip | shasum -a 256 -c -
python3 scripts/ingest_zip.py quarantine/tui-2026-10-15/uji/archives/uji-plugins-7edceeec13a6.zip quarantine/tui-2026-10-15/uji/source/uji-plugins --root uji-plugins-7edceeec13a679edf216a7ae6c2fa6a4bf084c25 --skip-symlinks
```

## uji-skills

- Repository: https://github.com/uji-labs/uji-skills
- Revision: `44a0011cae6e82c64d699a647b097207f1280bea`
- SHA-256: `10fa66cd111c12e2cd7ddf2c0790e11d113cf406061ca26c67da06dd36e13178`; ZIP bytes: 782; retained files: 1.
- Extract: `quarantine/tui-2026-10-15/uji/source/uji-skills`; excluded entries: 0.

```sh
mkdir -p quarantine/tui-2026-10-15/uji/archives
curl --disable --fail --location --output quarantine/tui-2026-10-15/uji/archives/uji-skills-44a0011cae6e.zip https://codeload.github.com/uji-labs/uji-skills/zip/44a0011cae6e82c64d699a647b097207f1280bea
printf '%s  %s\n' 10fa66cd111c12e2cd7ddf2c0790e11d113cf406061ca26c67da06dd36e13178 quarantine/tui-2026-10-15/uji/archives/uji-skills-44a0011cae6e.zip | shasum -a 256 -c -
python3 scripts/ingest_zip.py quarantine/tui-2026-10-15/uji/archives/uji-skills-44a0011cae6e.zip quarantine/tui-2026-10-15/uji/source/uji-skills --root uji-skills-44a0011cae6e82c64d699a647b097207f1280bea --skip-symlinks
```
