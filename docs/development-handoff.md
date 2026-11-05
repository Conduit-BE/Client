# Development handoff — 2026-11-06

## Preserved implementation

The Windows x64 DLL foundation, public C API, lifecycle, polling watcher, JSON validation, D3D12 overlay, F3 disk diagnostics, and standalone tests are in source control. **Minecraft hot reload is still unsupported.** Disk metadata is not the live game's control tree, and the watched directory is not automatically mounted as a resource pack.

Use [build.md](build.md) for native Linux tests, cross-compilation, and Windows/Wine smoke tests. Dependencies are pinned in CMake and fetched during configuration; local dependency checkouts are optional. Build outputs belong under `.local/build/`. The implementation was preserved without changes during this checkpoint.

## Investigation resume point

Start with [the legacy reload findings](reverse-engineering/1.26.60-legacy-reload-path.md), particularly **Callback R8 origin: registration and invocation**. [Hopper diagnostics](reverse-engineering/1.26.60-hopper-diagnostics.md) explain the successful small-file controls, the real-target PE loading bottleneck, and the Linux Ghidra fallback.

The validated callback-context path reaches an indirect wrapper producer at original RVA `0x88afa6`, with a conditional context override in dispatcher `0x41c190`. Neither source has a proven concrete table/type. The first meaningful target inside `0x6583170` remains unresolved. Do not substitute a nearby function pointer or inferred class layout. Further investigation is restricted to legacy `_ui_defs.json`; no runtime injection is authorized by these reproduction instructions.

The [reverse-engineering tools README](../tools/reverse-engineering/README.md) documents the preserved scripts and how to recreate a targeted Ghidra project on Linux. Function coordinates belong to the exact executable hash in the findings. Obtain that executable through the licensed local installation; game bytes are not distributed by this repository.

## Local artifacts that Git does not transfer

These paths remain on the original machine; clone/pull does not restore them:

- `.local/build/`: native and Windows builds, DLLs, test executables and CTest output.
- `.local/deps/`: optional MinHook and JSON dependency checkouts.
- `.local/wine-conduit/`: dedicated test Wine prefix and logs.
- `.local/tools/`: Ghidra archive and extracted Ghidra 12.1.4 installation.
- `.local/hopper-diagnostics/`: fixtures, REA Evidence JSON, private-display screenshots, logs and scoped Ghidra settings/cache.
- `.local/re-investigation/`: original-derived analysis copies, manifests, selected-function TSVs, filtered disassembly, decompiler output, local scripts, logs and scoped settings/cache. The findings summarize the relevant conclusions; raw captures are not in Git.
- `.local/minecraft-binaries/` and `.local/xodus-source/`: local game acquisition artifacts and acquisition-tool checkout/build.
- `/tmp/conduit-ghidra-12660/`: the saved `Bedrock12660Targeted` Ghidra project, plus the earlier incomplete original-import project. `/tmp` is disposable; do not rely on it surviving a reboot.
- `/tmp/conduit-spyglass-reference/`: reference source checkout; the inspected revision is recorded in the findings.
- The licensed BedrockOnLinux installation, launcher configuration, game data and installed Hopper/REA tools outside this repository.

If the exact raw evidence or saved Ghidra database is needed, retain a separate private backup before disposing of the old environment. Otherwise recreate the project from the matching licensed executable and committed tooling. Do not add proprietary executable bytes, derived images, raw disassembly/decompilation, credentials or machine-specific databases to Git.
