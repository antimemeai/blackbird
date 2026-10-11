# Sources and restoration

## Native sampler and Lua boundaries, 2026-10-11

`quarantine/resource-sampler-2026-10-11/` retains primary Linux proc documentation,
pinned XNU/libproc resource-acquisition source and the intact official Lua5.4.8
archive matching the installed Lua profile. Only lgc.c/lstate.c/lapi.c/lua.h were
extracted from Lua; no Git metadata, generated files or adopted dependency.
[The acquisition report](papers/2026-10-11-resource-sampler-references.md) records
URLs, SHA-256 identities, restoration commands and the distinction between XNU
source reference revision and installed kernel. The sampler remains a subplan;
the Lua study supports the focused authored-event plan attack.

## Beads native-integration study, 2026-10-07

Matching installed bd0.58.0 source is pinned to
ae14933db67a0f67da5a8fb69be72c2282ca0e73 under quarantine/beads-2026-10-07.
Intact shared archive, hash, acquisition URL and exact extraction/restoration
command are recorded in papers/2026-10-07-beads-integration-study.md. Nested Git
metadata/detritus removed from extraction; archived instructions are historical.
Study-only, no imported code/runtime/database dependency adopted.

## Fresh workflow study, 2026-10-07

A fresh NousResearch Hermes snapshot at a3ed4a173070 and the separate community
hermes-workflows plugin at9fc82fe73555 are retained under
quarantine/hermes-agent-2026-10-07 and quarantine/hermes-workflows-2026-10-07.
Older Hermes source remains intact. Exact revisions, intact archive paths and
hashes, extraction exclusions and restoration commands are in
[papers/workflows-2026-10-07/hermes-acquisition.json](papers/workflows-2026-10-07/hermes-acquisition.json).
Both are study-only; no installer, runtime or upstream tests were executed, and
no dependency selected. Archives live on the shared quarantine shelf.

## Terminal study additions, 2026-10-07

Deeper chat/render study adds FTXUI, Notcurses and libvterm. Exact revisions,
archive SHA-256 and restoration arguments are in
[native reference provenance](papers/2026-10-07-chat-native-reference.json) and
[C renderer provenance](papers/2026-10-07-render-reference-acquisition.json).
ZIPs remain intact in the same shared terminal-study archive shelf. Clean
extractions are `quarantine/ftxui`, `quarantine/notcurses`, `quarantine/libvterm`.
These are studied source references; no dependency was adopted or code executed.

Acquired official Xiaomi `MiMo-Code` source and Meta `muse-code-sdk` for the
[terminal UX study](papers/2026-10-07-terminal-ux-study.md). The latter contains
SDK/protocol source, not Muse's host/TUI implementation. Exact revisions, archive
SHA-256, roots, URLs, destinations and restoration arguments are in
[the acquisition manifest](papers/2026-10-07-terminal-reference-acquisition.json).
Intact ZIPs live in `../quarantine_proj/archives/arconaut-terminal-2026-10-07/`;
extracted snapshots are `quarantine/mimo-code/` and `quarantine/muse-code-sdk/`.
Extraction omitted Git metadata, filesystem detritus and symlinks using
`scripts/ingest_zip.py`; originals stay intact. Restore with destinations absent
using each manifest record's `restoration` command. Source was studied, not run
or adopted as a dependency.

Recorded 2026-09-29; reconstruction updated 2026-09-30. References teach patterns;
their code is not incorporated into the fresh implementation. Historical
instructions inside them are not current instructions.

## EDG compiler — 2026-10-01

Official [EDG compiler](https://github.com/edgcpp/compiler) source at
`89e67e07c7f6d0fc38622f9778d8e79f40deaa0f` is retained in
`quarantine/edg-compiler/`: 111,049 regular files (3,549,149,165 bytes) and 12
confined symlinks. The intact 244,854,866-byte ZIP is
`../quarantine_proj/archives/arconaut-edg-compiler-2026-10-01/edg-compiler-89e67e07c7f6.zip`,
SHA-256 `66df655bf9a518932787e265d04b680f3180bd7ee36cc2da15528352029e0947`.
The [exact catalog](papers/2026-10-01-edg-compiler-acquisition.json) records
immutable source, direct archive comparisons and an explicit ingestion override.
The [study](papers/2026-10-01-edg-compiler-study.md) distinguishes a proposed
independent language-checking lane from native compiler/runtime qualification.
No imported build, test, initializer, container or binary ran; no dependency adopted.

Generic ingestion initially omitted 46 legitimate compiler expectation filenames
beginning `._`, plus 12 functional symlinks. All were restored from the preserved
ZIP and compared directly: regular bytes/executable bits and symlink target
strings. Symlink destinations remain within the reference root; three point to
`Local_file.c`, created later by the upstream test checkout. The catalog retains
the initial exclusions and each restoration role. Neither source archive nor
shared ingester was modified.

Restore from Arconaut with the destination absent, then apply these exact
catalog-listed overrides (the generic ingest command alone is incomplete):

```sh
python3 scripts/ingest_zip.py ../quarantine_proj/archives/arconaut-edg-compiler-2026-10-01/edg-compiler-89e67e07c7f6.zip quarantine/edg-compiler --root compiler-89e67e07c7f6d0fc38622f9778d8e79f40deaa0f --skip-symlinks
python3 - <<'PY'
import json
from pathlib import Path
from zipfile import ZipFile
record = json.loads(Path('papers/2026-10-01-edg-compiler-acquisition.json').read_text())['references'][0]
root = Path(record['destination']).resolve()
with ZipFile(record['archive']) as archive:
    for entry in record['ingestion_override']['restored']:
        destination = root / entry['path']
        if destination.exists() or destination.is_symlink():
            raise RuntimeError(f'existing override destination: {destination}')
        destination.parent.mkdir(parents=True, exist_ok=True)
        archived = archive.getinfo(record['archive_root'] + '/' + entry['path'])
        payload = archive.read(archived)
        if entry['kind'] == 'confined symlink':
            target = payload.decode()
            if not (destination.parent / target).resolve().is_relative_to(root):
                raise RuntimeError(f'escaping symlink: {destination}')
            destination.symlink_to(target)
        else:
            destination.write_bytes(payload)
            destination.chmod((archived.external_attr >> 16) & 0o777 or 0o644)
PY
```

After the 2026-09-30 acquisition pass, the shelf has **104 external reference
directories** plus historical `quarantine/arconaut/`. These are references of
different kinds, not 104 independently implemented agents:

| Acquisition | Reference directories |
| --- | ---: |
| Initial three legacy imports and sixteen fresh frumentarii sources | 19 |
| Known agent/assistant/collaboration and supporting source pass | 36 |
| Newly discovered agents, support sources, and produced review material | 46 |
| K-Dense BYOK application source and public material | 1 |
| Claude Science public documentation/examples/distributions | 1 |
| Official Claude Code public distributions and release material | 1 |

## Quarantined Arconaut source

`quarantine/arconaut/` contains the entire assessed source at `3bf2056` plus the
previously untracked `lemonnotes.md`: 182 files. The implementation, Cargo/build/
lint configuration, June design and research documents, original instructions,
old `.beads/` records, and CI have their original source layout. The current
doctrine, assessments, journal, and reconstruction documents remain outside it.
No module has been selected for the new implementation.

The restorable ZIP is
`../quarantine_proj/archives/arconaut-3bf2056-source.zip`. It was generated from
the preserved Git source, with the unchanged untracked note added before
preservation. Its SHA-256 identity is:

```text
01fd58cc02d53de967e0be2ad69feee151efbcbdec46c799c65145c3af39ff03
```

Restore from this repository, with `quarantine/arconaut` absent:

```sh
python3 scripts/ingest_zip.py ../quarantine_proj/archives/arconaut-3bf2056-source.zip quarantine/arconaut --root arconaut
```

On another host, obtain the archive with that identity and use its path. The
reference contains no nested Git metadata or filesystem detritus. All retained
file bytes and executable bits were compared directly with the preserved ZIP.
The large original ZIP and source-history bundle below remain intact.

## Arconaut lineage

| Source | Observed state | Preservation |
| --- | --- | --- |
| `https://github.com/antimemeai/arconaut.git` | Public master `76ddf000657fcaf7feebeccddd20625881a3aa8c` | `origin/master` in this clone |
| `~/projects_old/miscellaneous/arconaut` | Master `3bf2056565eff537b380f4fd649d725f36d1c493`, 15 commits ahead, no configured remote | `legacy-local/master`, ancestor of assessment branch; portable bundle below |
| Same checkout, untracked `lemonnotes.md` | Design proposal not present in Git | `quarantine/arconaut/lemonnotes.md` and the new source ZIP |
| `~/projects_old/miscellaneous/arconaut.zip` | 211,400 ZIP entries; recorded master also `3bf2056` | Original archive left intact in place; not exhaustively compared with checkout |

The complete local master history was saved as
`../quarantine_proj/archives/arconaut-3bf2056.bundle`. This is an intact source
archive, not a reference checkout containing nested Git metadata.

To reconstruct the assessed source on another host, obtain that bundle and run
from a directory where `arconaut` does not exist:

```sh
git clone https://github.com/antimemeai/arconaut.git arconaut
git -C arconaut fetch /path/to/arconaut-3bf2056.bundle master:refs/remotes/legacy-local/master
git -C arconaut switch -c assessment-source legacy-local/master
```

The assessment reports and refreshed instructions are local reconstruction
artifacts, outside this source bundle. The original checkout remains live and intact.

## Reference corpus

[The catalog](papers/legacy-reference-catalog.json) records the locations,
origins, and revisions of all 53 legacy reference directories. Fifty-two contain
Git metadata in the original shelf; those originals are unchanged. Three clean
snapshots were imported here because this assessment directly studies them:

| Directory | Origin | Revision | Imported regular files |
| --- | --- | --- | --- |
| `quarantine/brush-shell/` | `https://github.com/reubeno/brush.git` | `f84ae8a74c43cad85824477d67ab3da5de972558` | 669 |
| `quarantine/mini-swe-agent/` | `https://github.com/SWE-agent/mini-swe-agent.git` | `2afd0fb81bacbf0aacfac9ded6f093c5acd0bf7c` | 220 |
| `quarantine/opencode/` | `https://github.com/sst/opencode.git` | `2006259a02a87edf9e37f253cbddf3188309026b` | 5,663 |

The corresponding ZIPs are in `../quarantine_proj/archives/arconaut-references/`,
named `<directory>-<first-12-revision-characters>.zip`. They were created with
`git archive` from clean original reference checkouts, preserving those selected
revisions. The ZIPs remain intact. Restore from this repo, with destinations absent:

```sh
python3 scripts/ingest_zip.py ../quarantine_proj/archives/arconaut-references/brush-shell-f84ae8a74c43.zip quarantine/brush-shell --root brush-shell --skip-symlinks
python3 scripts/ingest_zip.py ../quarantine_proj/archives/arconaut-references/mini-swe-agent-2afd0fb81bac.zip quarantine/mini-swe-agent --root mini-swe-agent --skip-symlinks
python3 scripts/ingest_zip.py ../quarantine_proj/archives/arconaut-references/opencode-2006259a02a8.zip quarantine/opencode --root opencode --skip-symlinks
```

If a ZIP is unavailable, clone its origin into a temporary directory, fetch the
exact revision if needed, then recreate it with:

```sh
git -C /path/to/temporary-clone archive --format=zip --prefix=NAME/ --output=/path/to/NAME-REV.zip FULL_REVISION
```

The importer was reused from the workspace's Rhizome utility with an explicit
`--skip-symlinks` option. It strips `.git`, `.entire`, macOS detritus, and Python
bytecode-cache directories; symbolic links are omitted for these study snapshots.
It retains regular files and executable bits and refuses an existing destination.
Root license files are retained; omitted symlinks remain in the preserved ZIPs.
No reference automation has been run.

Original reference checkouts remain unchanged on the original shelf. The broader
known-agent acquisition below supplements these three initial imports; the old
catalog still records historical locations and revisions, not current upstream HEAD.
Shared wiki: `../quarantine_proj/xx_wiki/`. Acquisition gaps belong in
`papers/SHOPPING_LIST.md` when a selected source cannot be obtained.


## Fresh frumentarii corpus — 2026-09-30

Sixteen pinned upstream source snapshots were acquired into ignored quarantine
for the expert-operator and self-improvement inquiry. No implementation is adopted.
All 33,036 retained regular files and executable bits match the preserved source
ZIPs directly. The ZIPs are intact GitHub codeload archives at exact commits.
Nested Git and filesystem detritus are omitted by the importer; symbolic links
are explicitly omitted and remain in the ZIPs. Exact omissions are recorded.

[The acquisition catalog](papers/2026-09-30-reference-acquisition.json) records
full revisions, original download URLs, archive identities and roots, file counts,
and omissions. Archives live in
`../quarantine_proj/archives/arconaut-frumentarii-2026-09-30/`.

| Reference | Revision prefix | Retained files | Research use |
| --- | --- | ---: | --- |
| [autoresearch](https://github.com/karpathy/autoresearch) | `228791fb499a` | 10 | Legible experiment intervention/evaluation loop |
| [dagger](https://github.com/dagger/dagger) | `4129d95c0c43` | 16,360 | Agent messaging, step scheduling, stateful recomposition (Go mechanism reference) |
| [gepa](https://github.com/gepa-ai/gepa) | `3f160c295000` | 603 | Shared evaluation API, code/policy candidates, search engines |
| [guile-fibers](https://github.com/wingo/fibers) | `f08253bafc30` | 80 | Scheme concurrency, channels, ports and scheduler lifetime |
| [ipykernel](https://github.com/ipython/ipykernel) | `cfa461d8afac` | 114 | Persistent execution, multiple clients and subshells |
| [jido](https://github.com/agentjido/jido) | `90b163eef87a` | 374 | BEAM strategies, directives, supervised signal processing |
| [letta-code](https://github.com/letta-ai/letta-code) | `96eb977b9477` | 2,427 | Memory versioning and managed compaction (TS mechanism reference) |
| [prime-agent](https://github.com/PrimeIntellect-ai/prime-agent) | `e75f59efc6f7` | 1,626 | Rust/Python partition, execution handles and continuity limits |
| [restate](https://github.com/restatedev/restate) | `2180b55410f6` | 1,519 | Queryable durable execution and deployment/version boundaries |
| [rlm](https://github.com/alexzhang13/rlm) | `d04208afbad2` | 179 | Executable context inspection and recursive model computation |
| [s6](https://github.com/skarnet/s6) | `67254f0c147f` | 367 | Small native process supervision and controller replacement |
| [sbcl](https://github.com/sbcl/sbcl) | `e0b6f381c2ab` | 2,127 | Compiled live Lisp, process primitives and image boundaries |
| [self-harness](https://github.com/qzzqzzb/Self-Harness) | `2720dbb3f522` | 24 | Failure-grounded candidate changes and evaluation/merge rules |
| [sly](https://github.com/joaotavora/sly) | `3ffa216d0818` | 98 | Live Lisp evaluation, compilation and loading |
| [spacetimedb](https://github.com/clockworklabs/SpacetimeDB) | `629e8c1e809a` | 6,987 | Transactional live state, subscriptions and durability boundaries |
| [squirreling](https://github.com/hyparam/squirreling) | `249f5bb27ed4` | 141 | Semantic query algorithms for studying an audit corpus (JS mechanism reference) |

Restore selected references from the Arconaut root after obtaining the preserved
archives with the catalog's SHA-256 identities. Each destination must be absent.
This example restores all sixteen using the existing importer:

```sh
python3 - <<'PY'
from pathlib import Path
import hashlib, json, subprocess
catalog = json.loads(Path("papers/2026-09-30-reference-acquisition.json").read_text())
for source in catalog["sources"]:
    archive = Path("..") / source["archive"]
    if hashlib.sha256(archive.read_bytes()).hexdigest() != source["sha256"]:
        raise SystemExit(f"Archive identity mismatch: {archive}")
    subprocess.run(["python3", "scripts/ingest_zip.py", str(archive),
                    source["destination"], "--root", source["archive_root"],
                    "--skip-symlinks"], check=True)
PY
```

On another host, adjust the archive location. To restore one reference, select
its catalog record and use the same importer arguments. Download URLs identify
the original source acquisition; the preserved ZIP identity identifies our artifact.
License files and historical instructions are retained as reference material.
No imported automation, installer, or program has been run.

## Complete known-agent pass and new discovery — 2026-09-30

At the operator's request, the source corpus now expands beyond the initial imports.
The catalogs distinguish actual agents, collaboration/orchestration machinery,
supporting runtimes, public product materials, and produced artifacts. Historical
fallbacks and unversioned local material are labeled explicitly. Existing snapshots
are not silently refreshed or replaced. Acquisition and source inspection do not
adopt a dependency or confer design authority on archived instructions.

- [Known-agent acquisition report](papers/2026-09-30-known-agent-acquisition.md)
  and [exact source catalog](papers/2026-09-30-known-agent-acquisition.json).
- [New discovery frumentarius](papers/2026-09-30-agent-discovery-frumentarii.md)
  and [exact source catalog](papers/2026-09-30-discovered-agent-acquisition.json).
- [Claude Science and scientific workbench acquisition](papers/2026-09-30-science-workbench-acquisition.md)
  and [artifact/documentation catalog](papers/2026-09-30-science-workbench-acquisition.json).
- [K-Dense BYOK source, documentation, and workflow templates](papers/2026-09-30-k-dense-byok-acquisition.md)
  and [exact reference catalog](papers/2026-09-30-k-dense-byok-acquisition.json).
- [Official Claude Code distribution acquisition](papers/2026-09-30-claude-code-distribution-acquisition.md)
  and [release/artifact catalog](papers/2026-09-30-claude-code-distribution-acquisition.json).
- [Unavailable acquisition material](papers/SHOPPING_LIST.md).

Known-agent ZIPs live in `../quarantine_proj/archives/arconaut-known-agents-2026-09-30/`;
discovered-source ZIPs in `../quarantine_proj/archives/arconaut-agent-discovery-2026-09-30/`;
scientific workbench bundles in
`../quarantine_proj/archives/arconaut-science-workbenches-2026-09-30/`.
Official Claude Code distributions have their own subdirectory under the known-agent
archive shelf, rather than being represented as vendor engine source.
These archives remain intact. Clean source snapshots omit nested Git metadata,
filesystem detritus, and symbolic links; omitted entries remain in their archives.
The source catalogs record direct retained-set/byte/executable-bit comparisons.
Science distinguishes vendor distributions, original documentation/examples,
derived readable material, and separately acquired public workflow source.

The owned `scripts/acquire_references.py` utility resolves immutable public source
revisions, preserves archives, stages ingestion, compares retained material directly,
and publishes an incremental catalog. Its input is a JSON list of `name`/`repository`
records, optionally with an immutable `revision` or explicitly labeled local fallback.
It leaves existing destinations unchanged and runs no imported code.

Restore acquired source snapshots from the Arconaut root after obtaining the exact
archives named in the catalogs; each selected destination must be absent. Select
one record instead of iterating all records to restore a single reference:

```sh
python3 - <<'PY'
from pathlib import Path
import hashlib, json, subprocess
catalogs = ("papers/2026-09-30-known-agent-acquisition.json",
            "papers/2026-09-30-discovered-agent-acquisition.json")
for catalog_path in catalogs:
    for source in json.loads(Path(catalog_path).read_text())["references"]:
        if source["status"] != "acquired":
            continue
        archive = Path(source["archive"])
        digest = hashlib.sha256()
        with archive.open("rb") as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(chunk)
        if digest.hexdigest() != source["sha256"]:
            raise SystemExit(f"Archive identity mismatch: {archive}")
        subprocess.run(["python3", "scripts/ingest_zip.py", str(archive),
                        source["destination"], "--root", source["archive_root"],
                        "--skip-symlinks"], check=True)
PY
```

On another host, adjust archive paths to the copied shelf. For Claude Science's
documentation/distribution bundle, use its catalog's archive/root identity and
the same importer into absent `quarantine/claude-science/`. The Science report
records exact restoration instructions and the scope of what was obtainable.
K-Dense BYOK's report supplies the corresponding source/public-material bundle
command into absent `quarantine/k-dense-byok/`; its intact upstream source ZIP is
also preserved separately. Its 27 repository documents and 326 workflow templates
are acquired source, rather than authenticated product exports.
The official Claude Code distribution report gives the corresponding bundle command
into absent `quarantine/claude-code-official-distribution/`. It preserves macOS ARM64
and Linux x64 native programs, installer/release material, and the canonical npm
launcher artifact without executing them. Full vendor engine source remains a
separate availability limit.

The known-source pass acquired 36 references and new discovery acquired 46.
Current Letta's public index and its deliberately historical V1 source share an
origin and remain distinct dated references; renamed aliases are not counted as
additional agents. Source exports do not automatically resolve Git submodules,
Git LFS assets, private services, or live state. The exact catalogs/report record
those boundaries. Official Claude Code public repository material and its
unofficial mirror remain distinct; mirror authenticity is unverified.

## Context Language Models — 2026-10-01

Added the operator-supplied [official CLM source](https://github.com/facebookresearch/context-language-models)
at `18dc11115f50f261233c5bba7937834491e307e8` to
`quarantine/context-language-models/`: 75 retained files, no omissions. The intact
ZIP and direct archive comparison are recorded in the
[acquisition catalog](papers/2026-10-01-context-language-models-acquisition.json).
The [source study](papers/2026-10-01-context-language-models-study.md) records archive
identity, restoration commands and design implications. No acquired code was executed
or adopted. This later addition is outside the completed 104-external-reference survey.

## C++ and Lua foundation grounding — 2026-10-01

Six additional references ground the selected stack and refit/audit obligations.
They are outside the completed agent-survey snapshot. No imported source, test,
installer or build was executed; no library was adopted.

| Reference | Identity | Extracted destination | Intact archive shelf |
| --- | --- | --- | --- |
| RuntimeCompiledCPlusPlus | `005c05145d98b87d974c36fc885003ea88bf3932` | `quarantine/runtime-compiled-cplusplus/` | `../quarantine_proj/archives/arconaut-native-reload-2026-10-01/` |
| fungos/cr | `1c3f8302320dee8206cf85d6aeb9e9a9cd78b527` | `quarantine/fungos-cr/` | Same native-reload shelf |
| Official Lua | 5.5.1, publisher archive checksum matched | `quarantine/lua-5.5.1/` | `../quarantine_proj/archives/arconaut-lua-embedding-2026-10-01/` |
| Official Lua tests | Exact 5.5.1 suite, publisher checksum matched | `quarantine/lua-5.5.1-tests/` | Same Lua-embedding shelf |
| sol2 | `c1f95a773c6f8f4fde8ca3efe872e7286afe4444` | `quarantine/sol2/` | Same Lua-embedding shelf |
| LevelDB | `7ee830d02b623e8ffe0b95d59a74db1e58da04c5` | `quarantine/leveldb/` | Shelf recorded in custody catalog below |

Exact source/download URLs, SHA-256 identities, archive roots, byte/file counts and
direct retained-byte/executable-bit comparisons are in the
[native catalog](papers/2026-10-01-native-reload-acquisition.json),
[Lua catalog](papers/2026-10-01-lua-embedding-acquisition.json), and
[custody catalog](papers/2026-10-01-refit-custody-acquisition.json).
Lua's original tarballs stay intact alongside derived ingestion ZIPs, with direct
comparison to the originals. RCC++'s GLFW Git submodule is absent from its upstream
source ZIP; this source-export boundary is recorded, not silently populated.

Restoration from the Arconaut root, with destinations absent:

```sh
python3 - <<'PY'
from pathlib import Path
import hashlib, json, subprocess
catalogs = ('papers/2026-10-01-native-reload-acquisition.json',
            'papers/2026-10-01-lua-embedding-acquisition.json',
            'papers/2026-10-01-refit-custody-acquisition.json')
for catalog in catalogs:
    for ref in json.loads(Path(catalog).read_text())['references']:
        if ref.get('status') != 'acquired':
            continue
        archive = Path(ref['archive'])
        if hashlib.sha256(archive.read_bytes()).hexdigest() != ref['sha256']:
            raise SystemExit(f'Archive identity mismatch: {archive}')
        subprocess.run(['python3', 'scripts/ingest_zip.py', str(archive),
                        ref['destination'], '--root', ref['archive_root'],
                        '--skip-symlinks'], check=True)
PY
```

The [native study](papers/2026-10-01-native-reload-grounding.md),
[Lua study](papers/2026-10-01-lua-embedding-grounding.md), and
[custody study](papers/2026-10-01-refit-custody-grounding.md) distinguish mechanisms
read from runtime qualification still required. Native author literature and
Godot/JENOVA documentation are cataloged separately; custody platform material
likewise records its exact source and reading limits. Archived instructions are inert.

## Lua 5.4.8 Linux qualification source

Intact archive: `../quarantine_proj/archives/lua-5.4.8.tar.gz`. Source:
https://www.lua.org/ftp/lua-5.4.8.tar.gz. SHA-256:
`4f18ddae154e793e46eeab727c59ef1c0c0c2b744e7b94219710d76f530629ae`.
Restore with `curl --fail --proto '=https' https://www.lua.org/ftp/lua-5.4.8.tar.gz
-o ../quarantine_proj/archives/lua-5.4.8.tar.gz`, then verify the digest.
`scripts/check-linux` builds this already-selected runtime in its disposable remote
workspace. No system installation or new runtime/version adoption is involved.

## Self-driven harness evolution frumentarii — 2026-10-06

Primary source/paper study for autodroit, useful-work campaigns and acceleration
measurement. Three acquisition catalogs provide original URLs, revision pins,
intact archive hashes/paths, restoration and actual reading limits:

- [Empirical acquisition/restoration](papers/frumentarii-2026-10-06/empirical/MANIFEST.md)
  and its machine-readable acquisition.json (repository cwd).
- [Harness acquisition/restoration](papers/frumentarii-2026-10-06/harness/manifest.json)
  and safe_extract.py (repository cwd). HELIX101 absolute fixture symlinks omitted
  from extraction, preserved in intact archive; omission is a reproduction limit.
- [Orchestration acquisition/restoration](papers/frumentarii-2026-10-06/orchestration/manifest.json)
  (paths/restoration relative to projects workspace).

Extracted sources are study-only, nested Git/filesystem metadata stripped; original
archives remain intact. No reference agents/tests were executed, no accounts used,
no dependencies adopted. Existing sources/pins explicitly reused in catalogs.
The synthesis and lane reports distinguish author benchmark claims, inspected code
mechanisms and unacquired implementation leads. Archived instructions remain inert.

## Optional integrations, thematic HUD and multiplayer — 2026-10-06

Study catalogs preserve pinned original archives, extracted source identities,
restoration commands and primary-document reading limits:

- [Mods manifest](papers/integrations-2026-10-06/mods/manifest.json): official
  Claude Code and playground sources; restoration uses repository cwd.
- [Feeds manifest](papers/integrations-2026-10-06/feeds/MANIFEST.md) and
  acquisition.json: MCP, CISA KEV and service documentation; repository cwd.
  Existing gptme plugin reference explicitly reused.
- [Teams manifest](papers/integrations-2026-10-06/teams/manifest.json): ICE,
  Commonly, Agent Room and official Claude changelog; projects workspace cwd.

Archives remain intact; extracted Git/filesystem metadata stripped. Acquired
instructions are historical reference. No reference programs/tests executed,
accounts authenticated, integrations installed or dependencies adopted. The
[synthesis](papers/integrations-2026-10-06/SYNTHESIS.md) separates inspected
mechanisms from proposed Arconaut design and future runtime qualification.

## G9 cryptographic transport decision study — 2026-10-07

Ignored `quarantine/g9-security/` contains RFC8446, OpenSSL3.5.0 verification-mode
API documentation and libsodium key-exchange documentation. Primary URLs, exact
SHA256 identities, restoration limitations and design/testing consequences are in
[papers/2026-10-07-peer-security-decision.md](papers/2026-10-07-peer-security-decision.md).
Acquired as study-only text, never executed or adopted. No production dependency
selected; G9 awaits explicit operator decision.

## phux — 2026-10-07

Operator-selected general multiplexing study: https://github.com/no-phux/phux,
commit `0fd4e511621f50c70c857ef0300b90fcd34a295f`. Extracted2626 reference files to ignored
`quarantine/phux/`; archive retained intact at
`../quarantine_proj/archives/arconaut-phux-2026-10-07/phux-0fd4e511621f50c70c857ef0300b90fcd34a295f.zip`
(20122943bytes; SHA256 `6ba142aaf70be41391dbc1684d017836aef32cba4fa8a9293cbb9a13598ead33`).
No nested Git history acquired; generic ingestion excludes metadata/detritus and
symlinks. Archived instructions carry no authority. No build/install/runtime executed.

Restore with absent destination:

```sh
curl -fL https://codeload.github.com/no-phux/phux/zip/0fd4e511621f50c70c857ef0300b90fcd34a295f -o ../quarantine_proj/archives/arconaut-phux-2026-10-07/phux-0fd4e511621f50c70c857ef0300b90fcd34a295f.zip
python3 scripts/ingest_zip.py ../quarantine_proj/archives/arconaut-phux-2026-10-07/phux-0fd4e511621f50c70c857ef0300b90fcd34a295f.zip quarantine/phux --root phux-0fd4e511621f50c70c857ef0300b90fcd34a295f --skip-symlinks
```

Study: papers/2026-10-07-phux-multiplexing.md. External optional service proposal,
not embedded dependency adoption or production qualification.

## P2P, multiplayer and E2EE frumentarii — 2026-10-07

Primary standards/literature and immutable reference archives acquired and studied
for the deferred multiplayer design. Source archives remain intact in shared
quarantine_proj; extracted refs are ignored and stripped of Git metadata/detritus.
No reference build/execution, networking dependency adoption or account use.
Sources, hashes, exact paths, restoration and acquisition gaps are recorded in:

- [P2P manifest](papers/networking-2026-10-07/p2p/MANIFEST.md)
- [Multiplayer references](papers/networking-2026-10-07/multiplayer/reference-manifest.json),
  [literature](papers/networking-2026-10-07/multiplayer/literature-manifest.json),
  [restoration](papers/networking-2026-10-07/multiplayer/RESTORE.md)
- [E2EE manifest](papers/networking-2026-10-07/e2ee/manifest.json),
  [restoration](papers/networking-2026-10-07/e2ee/RESTORE.md)

[Source-grounded synthesis](papers/networking-2026-10-07/SYNTHESIS.md) separates
identity, reachability, encryption, custody and shared-work authority.

## Lean durable-state performance references — 2026-10-07

Source-grounded storage/messaging study for startup and bounded memory, beyond
agent harness references. Acquired immutable LMDB700e10f91a65,
SQLite5af1b822f5da, TigerBeetlec95d7a53a3d0, Aeronad4baf8dcd5a and
Bitcaskd84c8d913713 archives. Originals remain intact in shared
`../quarantine_proj/archives/blackbird-storage-2026-10-07/`; extracted source is
ignored under `quarantine/storage-NAME-COMMITPREFIX/`, Git metadata/detritus and
symlinks omitted. Bitcask's original2010 paper remains in its source archive.
Existing LevelDB7ee830d02b62 also studied. Official SQLite/Git/TigerBeetle/Symas
documents are snapshotted under ignored `quarantine/storage-documents-2026-10-07/`.

Exact identities, SHA256, byte sizes, URLs and restoration/extraction arguments:
[references](papers/storage-performance-2026-10-07/references.json),
[documents](papers/storage-performance-2026-10-07/documents.json).
Restore to absent extraction destinations using each manifest's curl and owned
scripts/ingest_zip.py arguments. HTML documents may change upstream; their hashes
identify the acquired content, not a guarantee of future identical restoration.

[Study and proposed course](papers/storage-performance-2026-10-07/STUDY.md)
distinguishes compact current state, bounded recovery tail and on-demand archive.
No imported builds/tests/installers executed, runtime adopted or dependency selected;
Aeron's JVM code is mechanism reference only. Archived instructions are historical.

## Trajectory campaign reference — 2026-10-09

Entire CLI, https://github.com/entireio/cli, revision `b4d2443bf18e98ca72c21b52a44833d1f6ecedc8`.
Study-only extraction: `quarantine/entire-cli-b4d2443bf18e`; no reference commands executed.
Intact archive: `../quarantine_proj/archives/blackbird-trajectories-2026-10-09/entire-cli-b4d2443bf18e.zip`; SHA-256 `ad9354dfe989cc71fd83f6a2245e0b3467612e6131770e3b321adf016b88de59`.
Source: https://codeload.github.com/entireio/cli/zip/b4d2443bf18e98ca72c21b52a44833d1f6ecedc8. Restoration, with destination absent:

```sh
python3 scripts/ingest_zip.py ../quarantine_proj/archives/blackbird-trajectories-2026-10-09/entire-cli-b4d2443bf18e.zip quarantine/entire-cli-b4d2443bf18e --root cli-b4d2443bf18e98ca72c21b52a44833d1f6ecedc8 --skip-symlinks
```

Importer omits nested Git metadata, `.entire`, filesystem detritus, bytecode
caches and symlinks; archive remains intact. This is a reference, not a dependency.

## Running command jobs: platform references — 2026-10-09

Pinned upstream tmux `82abcd175cca43c671af3690cd0af74c4c75621c` and primary
POSIX/GNU/Darwin/Linux documents were acquired and studied for backgrounding and
foregrounding the same running command. Intact ZIPs remain in
`../quarantine_proj/archives/blackbird-command-jobs-platform-2026-10-09/`; clean
ignored source is in `quarantine/command-jobs-tmux/` and
`quarantine/command-jobs-platform-documents-2026-10-09/`. No dependency adopted or
imported code executed; archived instructions are historical only. Exact URLs,
revision/SDK identity, SHA-256, retained-byte comparisons and restoration limits:
[platform report](papers/2026-10-09-command-jobs/platform.md),
[source manifest](papers/2026-10-09-command-jobs/platform-references.json),
[document manifest](papers/2026-10-09-command-jobs/platform-documents.json), and
[restoration](papers/2026-10-09-command-jobs/PLATFORM_RESTORE.md). Five owned
primitive probes ran separately on macOS 26.6 arm64 and Neuroses Linux 6.8/glibc
2.39; these do not qualify Blackbird implementation.

## Native instrumentation study — 2026-10-10

Study-only snapshots under `quarantine/instrumentation-2026-10-10/`; source
archives intact, extracted nested Git metadata and filesystem detritus removed.
No library, SDK or source dependency adopted. See the local MANIFEST.md and
`papers/2026-10-10-instrumentation-native-mechanisms.md` for use and exact source
paths. Restoration downloads these pinned archives, checks SHA-256, extracts
with Python3.14 tarfile data filtering, and renames NAME-COMMIT to NAME.

| Reference | Commit | Archive URL | SHA-256 |
| --- | --- | --- | --- |
| lttng/lttng-ust | `5d11feb7dbd25862da163880f89acb6a6d88754c` | [archive](https://codeload.github.com/lttng/lttng-ust/tar.gz/5d11feb7dbd25862da163880f89acb6a6d88754c) | `c2d168abc8d8a2de917006ddc8c253ece97e03d5c77fd03a0d1966902bafd10e` |
| google/perfetto | `977bd034d59e01de0d4d202a2431c36c6e0e5a74` | [archive](https://codeload.github.com/google/perfetto/tar.gz/977bd034d59e01de0d4d202a2431c36c6e0e5a74) | `a34689008bf4f7b5b426320f2d980f01d035e23c03b6ed187d61d6a424d3695b` |
| wolfpld/tracy | `d55cee060180ad386aaa0b289880f5889cc5407d` | [archive](https://codeload.github.com/wolfpld/tracy/tar.gz/d55cee060180ad386aaa0b289880f5889cc5407d) | `2b793194bc4a27b4ef20e5dbfb5effff1c0521ff346a4869d0b5f87c240f14d2` |

Numeric source snapshots remain under the same directory's `numeric/` subdirectory,
with original tar.gz archives and extracted `NAME-COMMIT/` trees. Their local
MANIFEST.md is supplemented here so an ordinary clone can restore them.

| Reference | Commit | Archive URL | SHA-256 |
| --- | --- | --- | --- |
| HdrHistogram/HdrHistogram | `de84b0a7de2378abfc405da503bf4898e84ea98e` | [archive](https://codeload.github.com/HdrHistogram/HdrHistogram/tar.gz/de84b0a7de2378abfc405da503bf4898e84ea98e) | `9d2bb52abf1791e8bccd203b5e018c07c7da9043316911b100f1e7187d57ee10` |
| HdrHistogram/HdrHistogram_c | `8885476fc83fa362fec2fb2b5e9cd9544514976e` | [archive](https://codeload.github.com/HdrHistogram/HdrHistogram_c/tar.gz/8885476fc83fa362fec2fb2b5e9cd9544514976e) | `5efe708b67065756aaca5940fd957538d986df60d3685e091895f7ae2f0abd19` |
| giltene/wrk2 | `44a94c17d8e6a0bac8559b53da76848e430cb7a7` | [archive](https://codeload.github.com/giltene/wrk2/tar.gz/44a94c17d8e6a0bac8559b53da76848e430cb7a7) | `40fb577303ae83bfc211855880696b6e14daf1eaea2a684fe8cf2252c735b47e` |

Restoration example for LTTng (substitute each table's URL, hash and tree name;
numeric trees retain NAME-COMMIT rather than being renamed). Destination must
be absent. The data extraction filter requires Python3.12+; ingestion used3.14.3.

```sh
mkdir -p quarantine/instrumentation-2026-10-10/archives
curl -fL https://codeload.github.com/lttng/lttng-ust/tar.gz/5d11feb7dbd25862da163880f89acb6a6d88754c -o quarantine/instrumentation-2026-10-10/archives/lttng-ust-5d11feb7dbd25862da163880f89acb6a6d88754c.tar.gz
python3 - <<'PY'
from pathlib import Path
import hashlib
import shutil
import tarfile
base = Path('quarantine/instrumentation-2026-10-10')
archive = base / 'archives/lttng-ust-5d11feb7dbd25862da163880f89acb6a6d88754c.tar.gz'
assert hashlib.sha256(archive.read_bytes()).hexdigest() == 'c2d168abc8d8a2de917006ddc8c253ece97e03d5c77fd03a0d1966902bafd10e'
extracted = base / 'lttng-ust-5d11feb7dbd25862da163880f89acb6a6d88754c'
target = base / 'lttng-ust'
assert not extracted.exists() and not target.exists()
with tarfile.open(archive) as source:
    source.extractall(base, filter='data')
extracted.rename(target)
for entry in sorted(target.rglob('*'), key=lambda p: len(p.parts), reverse=True):
    if entry.name in {'.git', '.DS_Store', '__MACOSX'}:
        if entry.is_dir() and not entry.is_symlink():
            shutil.rmtree(entry)
        else:
            entry.unlink()
PY
```

The DTrace2004 paper is ignored at
`papers/instrumentation-2026-10-10/cantrill-dtrace-2004.pdf`, SHA-256
`52fc73e3ef2acd778f86f067d59a2ddff3e80f76631f4bda8362990a50a59283`.
Restore:

```sh
mkdir -p papers/instrumentation-2026-10-10
curl -fL https://static.usenix.org/publications/library/proceedings/usenix04/tech/general/full_papers/cantrill/cantrill.pdf -o papers/instrumentation-2026-10-10/cantrill-dtrace-2004.pdf
```

Five additional acquired PDFs (Monarch, DDSketch, KLL, t-digest and coordinated
omission slides), exact source versions, hashes and restoration commands are in
[the numeric literature report](papers/2026-10-10-instrumentation-literature.md).
Existing harness source identities/restoration remain in the2026-09-30 known
and discovered agent acquisition manifests; no duplicate archive was required.

## Raw metric emission: older Unix references — 2026-10-10

Study-only selected historical source under
`quarantine/instrumentation-2026-10-10/old-unix/`: Research Unix V7 pipe.c (1979),
Berkeley kern_ktrace.c7.9/ktrace.h7.3 (1990-06-28) and kdump.c1.9 (1990-06-29,
1988 copyright). Preserve complete downloaded TUHS HTML responses in originals/;
extract the single PRE block, decode HTML entities and retain the C source with
its original notices. No full operating-system archive, nested Git metadata,
third-party executable or dependency was acquired. A failed path response lives
in ignored context, not the extracted reference set.

[Exact URLs, original-response and extracted-source SHA-256 hashes](papers/2026-10-10-instrumentation-old-unix-sources.json)
are tracked; ignored MANIFEST.json repeats them beside the references. Restore
from the repository root with the following command; it verifies extracted
source bytes because a future TUHS page wrapper may change. Original-response
hashes preserve the exact acquisition identity.

```sh
python3 - <<'PYRESTORE'
import hashlib, html, json, re, urllib.request
from pathlib import Path
for item in json.loads(Path('papers/2026-10-10-instrumentation-old-unix-sources.json').read_text()):
    original = Path(item['original'])
    extracted = Path(item['extracted'])
    assert not original.exists() and not extracted.exists()
    raw = urllib.request.urlopen(item['url'], timeout=30).read()
    block = re.search(r'<pre>(.*?)</pre>', raw.decode('utf-8'), re.S)
    assert block, item['url']
    source = (html.unescape(block.group(1)).strip() + '\n').encode()
    assert hashlib.sha256(source).hexdigest() == item['extracted_sha256']
    original.parent.mkdir(parents=True, exist_ok=True)
    original.write_bytes(raw)
    extracted.write_bytes(source)
PYRESTORE
```

Older literature, actual read sections, ignored PDF source/version/hash table,
restoration and acquisition limits are in the
[raw-emission study](papers/2026-10-10-instrumentation-raw-emission-study.md).
No reference was built or executed. The previous producer-transform proposal is
superseded, not approved by the existence of these references.

## Raw transport and recorder source study 2026-10-10

`quarantine/raw-transport-2026-10-10/` preserves official qmail1.03,
daemontools0.76, s6/skalibs2.15.1.0 archives, POSIX2024 shared-memory/I/O/send
contracts, acquired C++ atomic rules, pinned Linux6.12 wait/pidfs source and
Apple socket/kqueue documentation. [Acquisition manifest](papers/2026-10-10-raw-transport-acquisitions.json)
records all16 original hashes, URLs and restoration arguments; the
[transport study](papers/2026-10-10-raw-transport-design-study.md) records exact
read source paths and extraction/restoration commands. qmail/daemontools paths
were read end to end. s6 is comparison material, not an adopted supervisor.

`quarantine/raw-recorder-2026-10-10/` independently preserves the daemontools
archive, official Linux man-pages6.19 archive and acquired djb/Apple documentation.
The [recorder manifest](papers/2026-10-10-raw-recorder-acquisitions.json) records
six acquisitions, studied-source hashes and restoration; the
[recorder study](papers/2026-10-10-raw-recorder-design-study.md) identifies the
actual logging/flush/restart mechanisms and their loss boundaries. Existing
SQLite/LevelDB remain comparison sources under their already recorded pins.

Archives remain intact; extracted references contain no nested Git metadata or
filesystem detritus. Nothing acquired was built, executed with operator accounts,
or selected as a dependency. The isolated probes are owned research code,
preserved with raw measurements in [their report](papers/2026-10-10-raw-emission-probes.md).

## Native storage lock and close study, 2026-10-11

`quarantine/native-storage-2026-10-11/` preserves original Linux6.12 `fs/locks.c`
and XNU `bsd/kern/kern_descrip.c` source files. Exact pinned URLs, byte lengths
and SHA-256 hashes are in the
[acquisition manifest](papers/2026-10-11-native-storage-acquisitions.json).
The Linux man-pages6.19 `close.2` reference is in the existing recorder acquisition.
These are read-only references; no code was built or adopted.

Restore the two files without changing their contents:

```sh
mkdir -p quarantine/native-storage-2026-10-11
curl -fL https://raw.githubusercontent.com/torvalds/linux/v6.12/fs/locks.c -o quarantine/native-storage-2026-10-11/linux-v6.12-locks.c
curl -fL https://raw.githubusercontent.com/apple-oss-distributions/xnu/f6217f891ac0bb64f3d375211650a4c1ff8ca1ea/bsd/kern/kern_descrip.c -o quarantine/native-storage-2026-10-11/xnu-kern_descrip.c
shasum -a 256 quarantine/native-storage-2026-10-11/*.c
```
