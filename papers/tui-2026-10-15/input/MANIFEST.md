# Expressive input quarantine manifest

Captured 2026-10-15. Source study only. No dependency selected, programs executed, firmware flashed, or controller/account connected. Paths below are relative to the Blackbird repository root. Restore into absent snapshot directories; the extractor refuses overwrites. `sources.tsv` gives full revisions, hashes, bytes, omission lists and exact commands. `documents.tsv` records primary documents and their restoration commands.

| Source | Pinned revision | Archive bytes | Retained files / bytes | License location |
| --- | --- | ---: | ---: | --- |
| [kanata](https://github.com/jtroo/kanata/tree/44edba27f9849b2494a4b9f5f003a4a5e932e277) | `44edba27f9849b2494a4b9f5f003a4a5e932e277` | 2056893 | 305 / 4313648 | LGPL-3.0; LICENSE; bundled components own notices |
| [kitty](https://github.com/kovidgoyal/kitty/tree/0d006c44b3d94a6ad7cb9ca11e1549af45095fe4) | `0d006c44b3d94a6ad7cb9ca11e1549af45095fe4` | 10594175 | 1114 / 28457293 | GPL-3.0; LICENSE; bundled subcomponents carry own notices |
| [liblo](https://github.com/radarsat1/liblo/tree/025f277275e6b81032a72dfb0b131adab80363e6) | `025f277275e6b81032a72dfb0b131adab80363e6` | 219455 | 69 / 761037 | LGPL-2.1; COPYING |
| [libremidi](https://github.com/celtera/libremidi/tree/291c7b9154b54a10f69bed9691fc0dca9dc6eeab) | `291c7b9154b54a10f69bed9691fc0dca9dc6eeab` | 16162399 | 1083 / 34725119 | BSD-2-Clause new work; MIT RtMidi/BSD-2-Clause ModernMidi; LICENSE.md; bundled own notices |
| [qmk](https://github.com/qmk/qmk_firmware/tree/7a1bbf37c5c07da4ea0a162bb139083a46ef40a1) | `7a1bbf37c5c07da4ea0a162bb139083a46ef40a1` | 31546822 | 22565 / 92697587 | GPL-2.0; LICENSE; bundled components own notices |
| [rtmidi](https://github.com/thestk/rtmidi/tree/aaa259546878456978d7b57210d0f910a90418b6) | `aaa259546878456978d7b57210d0f910a90418b6` | 656880 | 136 / 1233058 | MIT-style permissive; LICENSE |
| [smartknob](https://github.com/scottbez1/smartknob/tree/4eb988399c3fda6ffd3006772856093dfe9adb86) | `4eb988399c3fda6ffd3006772856093dfe9adb86` | 15204986 | 230 / 19079424 | Apache-2.0; LICENSE.txt; bundled own notices |

## Restoration

From the Blackbird root, these commands retrieve the immutable upstream archives and produce the same cleaned snapshots. SHA-256 values in the TSV identify the acquired archive bytes. If GitHub regenerates ZIP packaging, retained source can still match while its container hash changes; record that instead of replacing this capture. Source ZIPs do not recursively fetch Git submodules or Git LFS payloads. Nothing in these references is a build-ready dependency closure.

### kanata

```sh
mkdir -p quarantine/tui-2026-10-15/input/archives && curl --disable --fail --location --output quarantine/tui-2026-10-15/input/archives/kanata-44edba27f984.zip https://codeload.github.com/jtroo/kanata/zip/44edba27f9849b2494a4b9f5f003a4a5e932e277 && python3 scripts/ingest_zip.py quarantine/tui-2026-10-15/input/archives/kanata-44edba27f984.zip quarantine/tui-2026-10-15/input/kanata --root kanata-44edba27f9849b2494a4b9f5f003a4a5e932e277 --skip-symlinks
```

Archive SHA-256: `a4d8a5ae6e5551c6f579d247128f83f714b308c1da9fdb1e76b71043f4418714`.

Omitted entries: none.

### kitty

```sh
mkdir -p quarantine/tui-2026-10-15/input/archives && curl --disable --fail --location --output quarantine/tui-2026-10-15/input/archives/kitty-0d006c44b3d9.zip https://codeload.github.com/kovidgoyal/kitty/zip/0d006c44b3d94a6ad7cb9ca11e1549af45095fe4 && python3 scripts/ingest_zip.py quarantine/tui-2026-10-15/input/archives/kitty-0d006c44b3d9.zip quarantine/tui-2026-10-15/input/kitty --root kitty-0d006c44b3d94a6ad7cb9ca11e1549af45095fe4 --skip-symlinks
```

Archive SHA-256: `8eb2b2845d8d31070d2340e24179bbbb38cf6001e6306bc8acf5c9dc6c8c1ec9`.

Omitted entries: kitty-0d006c44b3d94a6ad7cb9ca11e1549af45095fe4/terminfo/78.

### liblo

```sh
mkdir -p quarantine/tui-2026-10-15/input/archives && curl --disable --fail --location --output quarantine/tui-2026-10-15/input/archives/liblo-025f277275e6.zip https://codeload.github.com/radarsat1/liblo/zip/025f277275e6b81032a72dfb0b131adab80363e6 && python3 scripts/ingest_zip.py quarantine/tui-2026-10-15/input/archives/liblo-025f277275e6.zip quarantine/tui-2026-10-15/input/liblo --root liblo-025f277275e6b81032a72dfb0b131adab80363e6 --skip-symlinks
```

Archive SHA-256: `add95357099601f99b6fd0d025d086d75e42a91e0e3f3b8a6cf5daba9319dc15`.

Omitted entries: none.

### libremidi

```sh
mkdir -p quarantine/tui-2026-10-15/input/archives && curl --disable --fail --location --output quarantine/tui-2026-10-15/input/archives/libremidi-291c7b9154b5.zip https://codeload.github.com/celtera/libremidi/zip/291c7b9154b54a10f69bed9691fc0dca9dc6eeab && python3 scripts/ingest_zip.py quarantine/tui-2026-10-15/input/archives/libremidi-291c7b9154b5.zip quarantine/tui-2026-10-15/input/libremidi --root libremidi-291c7b9154b54a10f69bed9691fc0dca9dc6eeab --skip-symlinks
```

Archive SHA-256: `bc0240f42140ae912b29d26838f5df6b1ea95ba3c14a0b670f96c36e3928e2da`.

Omitted entries: libremidi-291c7b9154b54a10f69bed9691fc0dca9dc6eeab/examples/rawio_esp32_serial/libremidi.

### qmk

```sh
mkdir -p quarantine/tui-2026-10-15/input/archives && curl --disable --fail --location --output quarantine/tui-2026-10-15/input/archives/qmk-7a1bbf37c5c0.zip https://codeload.github.com/qmk/qmk_firmware/zip/7a1bbf37c5c07da4ea0a162bb139083a46ef40a1 && python3 scripts/ingest_zip.py quarantine/tui-2026-10-15/input/archives/qmk-7a1bbf37c5c0.zip quarantine/tui-2026-10-15/input/qmk --root qmk_firmware-7a1bbf37c5c07da4ea0a162bb139083a46ef40a1 --skip-symlinks
```

Archive SHA-256: `622ca45899b04a9a109536890b3f68eaa31cb127415659bac2edad5210f7286c`.

Omitted entries: none.

### rtmidi

```sh
mkdir -p quarantine/tui-2026-10-15/input/archives && curl --disable --fail --location --output quarantine/tui-2026-10-15/input/archives/rtmidi-aaa259546878.zip https://codeload.github.com/thestk/rtmidi/zip/aaa259546878456978d7b57210d0f910a90418b6 && python3 scripts/ingest_zip.py quarantine/tui-2026-10-15/input/archives/rtmidi-aaa259546878.zip quarantine/tui-2026-10-15/input/rtmidi --root rtmidi-aaa259546878456978d7b57210d0f910a90418b6 --skip-symlinks
```

Archive SHA-256: `3b94c0e0ad5b673ddb11a0834e78a4ed9e367572beb8dc0f8745c8e410682382`.

Omitted entries: rtmidi-aaa259546878456978d7b57210d0f910a90418b6/examples/android/app/src/main/cpp/RtMidi.cpp;rtmidi-aaa259546878456978d7b57210d0f910a90418b6/examples/android/app/src/main/cpp/RtMidi.h;rtmidi-aaa259546878456978d7b57210d0f910a90418b6/examples/android/app/src/main/java/com/yellowlab/rtmidi/MidiDeviceOpenedListener.java;rtmidi-aaa259546878456978d7b57210d0f910a90418b6/examples/ios/RtMidiExample/RtMidi.cpp;rtmidi-aaa259546878456978d7b57210d0f910a90418b6/examples/ios/RtMidiExample/RtMidi.h.

### smartknob

```sh
mkdir -p quarantine/tui-2026-10-15/input/archives && curl --disable --fail --location --output quarantine/tui-2026-10-15/input/archives/smartknob-4eb988399c3f.zip https://codeload.github.com/scottbez1/smartknob/zip/4eb988399c3fda6ffd3006772856093dfe9adb86 && python3 scripts/ingest_zip.py quarantine/tui-2026-10-15/input/archives/smartknob-4eb988399c3f.zip quarantine/tui-2026-10-15/input/smartknob --root smartknob-4eb988399c3fda6ffd3006772856093dfe9adb86 --skip-symlinks
```

Archive SHA-256: `8d7b474d1aeb8442a864026f71c52027c23e37043a75e8b859e5dae9a6374a23`.

Omitted entries: none.

## Acquisition handling

The existing `scripts/acquire_references.py` fetched seven immutable ZIPs; `scripts/ingest_zip.py` omitted nested Git/filesystem detritus and symbolic links. The helper directly compared retained file sets, file bytes and executable bits against each ZIP. Archives remain intact under `quarantine/tui-2026-10-15/input/archives/`. Acquisition input/catalog are ignored there. No source snapshot was edited. License descriptions are source-study metadata, not a determination of licensing for hypothetical reuse.

The xterm server returned a Brotli entity even without an Accept-Encoding request. That first body is preserved as `xterm-ctlseqs.html.br`; a second request using curl decompression produced readable HTML. Both have separate document manifest rows. Unversioned web pages are dated content captures: exact historical replay requires these retained bytes, because their URLs may change. All other requested downloads succeeded. The Linux force-feedback page explicitly requests its two diagrams accompany redistribution; both are captured.

## Primary documents

| Document | Bytes | Captured file | Status |
| --- | ---: | --- | --- |
| [fixterms](https://www.leonerd.org.uk/hacks/fixterms/) | 14693 | `quarantine/tui-2026-10-15/input/documents/fixterms.html` | acquired |
| [osc-1.0](https://opensoundcontrol.stanford.edu/spec-1_0.html) | 20659 | `quarantine/tui-2026-10-15/input/documents/osc-1.0.html` | acquired |
| [linux-force-feedback](https://www.kernel.org/doc/html/latest/input/ff.html) | 22064 | `quarantine/tui-2026-10-15/input/documents/linux-force-feedback.html` | acquired |
| [libremidi-smc2024](https://smcnetwork.org/smc2024/papers/SMC2024_paper_id104.pdf) | 646027 | `quarantine/tui-2026-10-15/input/documents/libremidi-smc2024.pdf` | acquired |
| [kitty-keyboard](https://sw.kovidgoyal.net/kitty/keyboard-protocol/) | 244916 | `quarantine/tui-2026-10-15/input/documents/kitty-keyboard.html` | acquired |
| [xterm-ctlseqs](https://invisible-island.net/xterm/ctlseqs/ctlseqs.html) | 293655 | `quarantine/tui-2026-10-15/input/documents/xterm-ctlseqs.html` | acquired |
| [linux-ff-shape](https://www.kernel.org/doc/html/latest/_images/shape.svg) | 5679 | `quarantine/tui-2026-10-15/input/documents/linux-ff-shape.svg` | acquired |
| [linux-ff-interactive](https://www.kernel.org/doc/html/latest/_images/interactive.svg) | 3399 | `quarantine/tui-2026-10-15/input/documents/linux-ff-interactive.svg` | acquired |
| [xterm-ctlseqs-wire-br](https://invisible-island.net/xterm/ctlseqs/ctlseqs.html) | 52003 | `quarantine/tui-2026-10-15/input/documents/xterm-ctlseqs.html.br` | acquired |
| [ableton-control-mapping](https://www.ableton.com/en/manual/midi-and-key-remote-control/) | 264622 | `quarantine/tui-2026-10-15/input/documents/ableton-control-mapping.html` | acquired |
| [midi1-detail](https://drive.google.com/uc?export=download&id=1ewRrvMEFRPlKon6nfSCxqnTMEu70sz0c) | 550787 | `quarantine/tui-2026-10-15/input/documents/midi1-detail.pdf` | acquired |
| [midi2-ump](https://drive.google.com/uc?export=download&id=1l2L5ALHj4K9hw_LalQ2jJZBMXDxc9Uel) | 4941141 | `quarantine/tui-2026-10-15/input/documents/midi2-ump.pdf` | acquired |

MIDI 1 and UMP/MIDI 2 PDFs came from the public Download links on MIDI Association standard pages. They are acquired and identified by full hashes, but not deeply reviewed in this report. Ableton manual text is a reference for relative encoders, takeover, mapping inspection and output feedback; it does not define all manufacturers’ controller protocols.
