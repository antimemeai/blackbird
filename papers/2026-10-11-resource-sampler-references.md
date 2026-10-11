# Resource sampler and Lua-boundary references

Study-only source; no production dependency or source copied into owned code.

- `linux-proc.html`: https://www.kernel.org/doc/html/latest/filesystems/proc.html
  SHA-256 `17d8f9f29e495697a07c0774bbb8576465be1154771a643c32e8c09eb7f35237`.
  Restore: `curl -fL https://www.kernel.org/doc/html/latest/filesystems/proc.html -o quarantine/resource-sampler-2026-10-11/linux-proc.html`.
- `xnu-proc_info.c`: https://raw.githubusercontent.com/apple-oss-distributions/xnu/f6217f891ac0bb64f3d375211650a4c1ff8ca1ea/bsd/kern/proc_info.c
  SHA-256 `557092f14fbad3341100f59e0391d2e812c54b79a7404c89b2af97471ad67082`.
  Restore: `curl -fL https://raw.githubusercontent.com/apple-oss-distributions/xnu/f6217f891ac0bb64f3d375211650a4c1ff8ca1ea/bsd/kern/proc_info.c -o quarantine/resource-sampler-2026-10-11/xnu-proc_info.c`.
- `xnu-resource.h`: https://raw.githubusercontent.com/apple-oss-distributions/xnu/f6217f891ac0bb64f3d375211650a4c1ff8ca1ea/bsd/sys/resource.h
  SHA-256 `7c94d99c05e5b85662acec60bffd0168981ba9d1a98de853bdf56395d147e2a4`.
  Restore: `curl -fL https://raw.githubusercontent.com/apple-oss-distributions/xnu/f6217f891ac0bb64f3d375211650a4c1ff8ca1ea/bsd/sys/resource.h -o quarantine/resource-sampler-2026-10-11/xnu-resource.h`.
- `xnu-bsd_kern.c`: https://raw.githubusercontent.com/apple-oss-distributions/xnu/f6217f891ac0bb64f3d375211650a4c1ff8ca1ea/osfmk/kern/bsd_kern.c
  SHA-256 `670dcdf6feb73845b102965c8dfab6175f87893bf0b7ca9fa450f38cc83ba19c`.
  Restore: `curl -fL https://raw.githubusercontent.com/apple-oss-distributions/xnu/f6217f891ac0bb64f3d375211650a4c1ff8ca1ea/osfmk/kern/bsd_kern.c -o quarantine/resource-sampler-2026-10-11/xnu-bsd_kern.c`.
- `libproc.h`: https://raw.githubusercontent.com/apple-oss-distributions/xnu/f6217f891ac0bb64f3d375211650a4c1ff8ca1ea/libsyscall/wrappers/libproc/libproc.h
  SHA-256 `37c7629819a18c5ef10d19a9bea00eb7e0bd31e0b127ecd2ae108c74f47c39e2`.
  Restore: `curl -fL https://raw.githubusercontent.com/apple-oss-distributions/xnu/f6217f891ac0bb64f3d375211650a4c1ff8ca1ea/libsyscall/wrappers/libproc/libproc.h -o quarantine/resource-sampler-2026-10-11/libproc.h`.
- `lua-5.4.8.tar.gz`: https://www.lua.org/ftp/lua-5.4.8.tar.gz
  SHA-256 `4f18ddae154e793e46eeab727c59ef1c0c0c2b744e7b94219710d76f530629ae`.
  Restore: `curl -fL https://www.lua.org/ftp/lua-5.4.8.tar.gz -o quarantine/resource-sampler-2026-10-11/lua-5.4.8.tar.gz`.

Lua archive retained intact; only lgc.c/lstate.c/lapi.c/lua.h extracted. No Git metadata or build files.
Restore extraction: `tar -xzf quarantine/resource-sampler-2026-10-11/lua-5.4.8.tar.gz -C quarantine/resource-sampler-2026-10-11 lua-5.4.8/src/lgc.c lua-5.4.8/src/lstate.c lua-5.4.8/src/lapi.c lua-5.4.8/src/lua.h`.
Pinned XNU source hashes identify exact studied bytes, not the installed kernel revision.

- `xnu-kern_resource.c`: https://raw.githubusercontent.com/apple-oss-distributions/xnu/f6217f891ac0bb64f3d375211650a4c1ff8ca1ea/bsd/kern/kern_resource.c
  SHA-256 `ffd547faab964b7860476a3b1e1c82525389666450b08eda19f9f5229eddd054`.
  Restore: `curl -fL https://raw.githubusercontent.com/apple-oss-distributions/xnu/f6217f891ac0bb64f3d375211650a4c1ff8ca1ea/bsd/kern/kern_resource.c -o quarantine/resource-sampler-2026-10-11/xnu-kern_resource.c`.

XNU source URLs pinned to commit `f6217f891ac0bb64f3d375211650a4c1ff8ca1ea`; each pinned URL was fetched and its bytes matched the acquired hash.
