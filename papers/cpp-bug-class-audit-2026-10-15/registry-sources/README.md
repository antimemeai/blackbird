# Registry and reclamation sources

Read the [class survey](../REGISTRIES.md) for findings and applicable mechanisms.
[MANIFEST.json](MANIFEST.json) contains the exact author, source URL, revision,
license, UTC acquisition timestamp, SHA256, byte length and restoration command
for each intact acquired original. Restore from repository root after creating
`quarantine/cpp-bug-class-audit-2026-10-15/registries/`; run the manifest's curl
commands and compare the listed hashes. These files are ignored reference
material. They are not production dependencies or copied implementation.

Studied source subsets:

- RocksDB v9.10.0 `cache/lru_cache.cc`: retire under mutex, free/notify outside;
  pinned-versus-evictable state; strict insertion capacity failure. Apache/GPL
  license texts preserved alongside the inherited LevelDB BSD notice.
- LevelDB1.23 `util/cache.cc`: explicit in-use/LRU partition and guarded fields;
  counterexample where a deleter can run under lock and pinned usage exceeds the
  nominal capacity. Its BSD license is preserved.
- Linux v6.12 `Documentation/core-api/{kref,workqueue}.rst` and
  `include/linux/lockdep.h`: actual lifetime references, release ordering,
  forward progress despite bounded execution resources, assertions such as
  `lockdep_assert_not_held`. Kernel lockdep is a study mechanism, not a portable
  Mac userspace lint option. COPYING retained; the two RST originals have no
  file-level SPDX notice, which the manifest explicitly records.
- LLVM llvmorg-18.1.8 ThreadSafetyAnalysis and two clang-tidy check manuals:
  exact baseline diagnostic capabilities/limitations for the Linux compiler;
  source patterns also fit Clang23. Root independently probes installed Clang
  behavior. LLVM license retained. No claim that a generic warning proves
  asynchronous progress or semantic retention.

Only these selected files were acquired; full repository archives were neither
needed nor acquired. Originals remain byte-for-byte unchanged. No binaries,
examples or upstream tests were executed. No new library, compiler flag or rule
was adopted by this research.
